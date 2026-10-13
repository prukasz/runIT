"""Build the graphify graph with a preprocessed C pass.

tree-sitter does not expand macros, so headers full of X-macros and attribute
macros (e.g. components/core/errors/errors.h) produce no symbols, and every use of
their types becomes a placeholder node. This script:

1. preprocesses each project .c file with the ESP-IDF toolchain (flags taken from
   build/compile_commands.json, output kept with line markers),
2. extracts symbols from the expanded text,
3. maps every node back to its original file and line via the markers,
4. keeps only project code (components/, main/) and drops ESP-IDF and placeholders,
5. merges with the non-C AST pass and the cached semantic (doc) results, then
   writes graphify-out/ the same way the normal pipeline does.

Run from the repo root with graphify's Python:
    $(cat graphify-out/.graphify_python) tools/graphify_pp.py [--only PREFIX] [--dry-run]
"""
import argparse
import bisect
import json
import os
import re
import shlex
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from graphify.analyze import god_nodes, suggest_questions, surprising_connections
from graphify.build import build_from_json
from graphify.cache import check_semantic_cache
from graphify.cluster import cluster, score_all
from graphify.detect import detect, save_manifest
from graphify.extract import extract
from graphify.export import to_json
from graphify.ids import make_id
from graphify.paths import out_path
from graphify.report import generate
from graphify.watch import _read_build_excludes, _read_build_gitignore

ROOT = Path('.').resolve()
WORK = ROOT / 'build' / 'graphify_pp'
OUT = ROOT / 'graphify-out'
PROJECT_DIRS = ('components/', 'main/')
SPEC = 'C:/Users/Lukasz/.claude-private/skills/graphify/references/extraction-spec.md'


def rel(p):
    s = os.path.normpath(str(p).replace('\\', '/')).replace('\\', '/')
    root = str(ROOT).replace('\\', '/') + '/'
    if s.lower().startswith(root.lower()):
        s = s[len(root):]
    return s


def compile_args(entry):
    # Keep escaped quotes (\") intact; turn Windows path separators into '/'.
    cmd = re.sub(r'\\(?!["\\])', '/', entry['command'])
    return shlex.split(cmd)


def preprocess(job):
    """Run the preprocessor on one unit. Returns (rel_file, output_path_or_None, error_line)."""
    f, entry = job
    out, skip = [], False
    args = compile_args(entry)
    i = 0
    while i < len(args):
        a = args[i]
        if a in ('-o', '-MF', '-MT', '-MQ'):
            i += 2
            continue
        if a in ('-c', '-MD', '-MMD') or a.startswith(('-MF', '-MT', '-MQ')):
            i += 1
            continue
        out.append(a)
        i += 1
    out += ['-E', '-w', entry['file'].replace('\\', '/')]
    res = subprocess.run(out, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if res.returncode != 0:
        return f, None, (res.stderr.strip().splitlines() or ['?'])[0]
    dst = WORK / 'pp' / (f[:-2].replace('/', '__') + '.c')
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(res.stdout, encoding='utf-8')
    return f, dst, None


def line_markers(path):
    """Sorted (preprocessed line, original line, original file) for '# N "file"' markers."""
    marks = []
    for i, line in enumerate(path.read_text(encoding='utf-8', errors='replace').split('\n'), 1):
        m = re.match(r'# (\d+) "([^"]+)"', line)
        if m:
            marks.append((i, int(m.group(1)), m.group(2)))
    return marks


def locate(marks, keys, lineno):
    """Map a preprocessed line to (project file, original line), or None if not project code."""
    i = bisect.bisect_right(keys, lineno) - 1
    if i < 0:
        return None
    pl, ol, f = marks[i]
    if f.startswith('<'):
        return None
    f = rel(f)
    if not f.startswith(PROJECT_DIRS):
        return None
    return f, ol + (lineno - pl - 1)


DECL_SKIP = ('return', 'typedef', 'if', 'while', 'for', 'switch', 'else', 'case', 'do')


def declarations(rel_file):
    """Names the expansion does not give a location: typedefs, struct/enum tags, macros,
    and (headers only) column-0 function prototypes. Yields (name, line, kind)."""
    is_header = rel_file.endswith('.h')
    lines = (ROOT / rel_file).read_text(encoding='utf-8', errors='replace').split('\n')
    for i, raw in enumerate(lines, 1):
        s = raw.strip()
        m = re.match(r'#\s*define\s+(\w+)', s)
        if m:
            yield m.group(1), i, 'macro'
        elif s.startswith('typedef'):
            fp = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', s)
            last = re.search(r'(\w+)\s*(\[[^\]]*\]\s*)?;\s*$', s)
            name = fp.group(1) if fp else (last.group(1) if last else None)
            if name:
                yield name, i, 'typedef'
        else:
            m = re.match(r'(?:struct|enum|union)\s+(\w+)\s*\{', s)
            if m:
                yield m.group(1), i, 'type'
            elif is_header and raw == raw.lstrip() and raw[:1].isalpha():
                m = re.match(r'(?:[\w\s\*]+?)\b(\w+)\s*\([^;{]*\)\s*;\s*$', s)
                if m and not s.startswith(DECL_SKIP) and '=' not in s:
                    yield m.group(1), i, 'function'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='', help='only C units whose path starts with this prefix')
    ap.add_argument('--dry-run', action='store_true', help='print stats, write nothing to graphify-out/')
    ap.add_argument('--update', action='store_true',
                    help='same as a full run: always rebuilds everything (about 30 s), so it '
                         'replaces /graphify . --update for this repo')
    args = ap.parse_args()

    det = detect(ROOT, extra_excludes=_read_build_excludes(out_path()),
                 gitignore=_read_build_gitignore(out_path()))

    cc = json.loads((ROOT / 'build' / 'compile_commands.json').read_text(encoding='utf-8'))
    units = {}
    for entry in cc:
        f = rel(entry['file'])
        if f.endswith('.c') and f.startswith(PROJECT_DIRS) and f.startswith(args.only):
            units.setdefault(f, entry)
    print(f'C units: {len(units)}')

    # 1. preprocess
    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(preprocess, units.items()))
    pp_files = [dst for _, dst, _ in results if dst]
    failed = [(f, err) for f, dst, err in results if not dst]
    print(f'preprocessed: {len(pp_files)}, failed: {len(failed)}')
    for f, err in failed[:5]:
        print(f'  FAILED {f}: {err}')

    # 2. extract the expanded units
    pp_ast = extract(pp_files, cache_root=ROOT, root=ROOT) if pp_files else {'nodes': [], 'edges': []}
    print(f'expanded AST: {len(pp_ast["nodes"])} nodes, {len(pp_ast["edges"])} edges')

    # 3. map nodes back to their original file and line
    markers = {}
    for dst in pp_files:
        marks = line_markers(dst)
        markers[rel(dst)] = (marks, [m[0] for m in marks])
    old2new, c_nodes = {}, {}
    # declarations the expansion does not locate (typedefs, tags, macros, header prototypes)
    by_name = {}
    project_files = {rel(p) for d in ('components', 'main') for p in (ROOT / d).rglob('*')
                     if p.suffix in ('.c', '.h')}
    for f in sorted(project_files):
        for name, line, kind in declarations(f):
            nid = make_id(f.rsplit('.', 1)[0], name)
            by_name.setdefault(name, set()).add(nid)
            c_nodes.setdefault(nid, {'id': nid, 'label': name + ('()' if kind == 'function' else ''),
                                     'file_type': 'code', 'source_file': f,
                                     'source_location': f'L{line}', '_origin': 'ast'})
    for n in pp_ast['nodes']:
        loc = re.match(r'L(\d+)$', n.get('source_location') or '')
        unit = str(n.get('source_file') or '')
        label = n.get('label', '')
        if not loc and label in by_name and len(by_name[label]) == 1:
            # located nowhere in the expansion: bind to the single project declaration by name
            old2new[n['id']] = next(iter(by_name[label]))
            continue
        if not loc or unit not in markers or not label or label.endswith(('.c', '.h')):
            continue
        marks, keys = markers[unit]
        hit = locate(marks, keys, int(loc.group(1)))
        if not hit:
            continue
        f, line = hit
        name = re.sub(r'\(\)$', '', label).strip()
        new_id = make_id(f.rsplit('.', 1)[0], name)
        old2new[n['id']] = new_id
        c_nodes.setdefault(new_id, {'id': new_id, 'label': label, 'file_type': 'code',
                                    'source_file': f, 'source_location': f'L{line}',
                                    '_origin': 'ast'})
    c_edges, seen = [], set()
    why = Counter()
    for e in pp_ast['edges']:
        s, t = old2new.get(e['source']), old2new.get(e['target'])
        if not s or not t:
            why['unmapped endpoint'] += 1
            continue
        if s == t or (s, t, e.get('relation')) in seen:
            why['self or duplicate'] += 1
            continue
        seen.add((s, t, e.get('relation')))
        c_edges.append({**{k: v for k, v in e.items() if k not in ('source', 'target')},
                        'source': s, 'target': t,
                        'source_file': c_nodes[s]['source_file'], '_origin': 'ast'})
    print(f'expanded edges dropped: {dict(why)}')
    headers_with_symbols = {n['source_file'] for n in c_nodes.values() if n['source_file'].endswith('.h')}
    all_headers = {rel(p) for d in ('components', 'main') for p in (ROOT / d).rglob('*.h')}
    print(f'C symbols: {len(c_nodes)} nodes, {len(c_edges)} edges; '
          f'headers with symbols: {len(headers_with_symbols)}/{len(all_headers)}')

    # 4. non-C code through the normal AST pass
    non_c = [f for f in det['files'].get('code', []) if not rel(f).endswith(('.c', '.h'))]
    other = extract([ROOT / rel(f) for f in non_c], cache_root=ROOT, root=ROOT) if non_c \
        else {'nodes': [], 'edges': []}
    print(f'non-C AST: {len(other["nodes"])} nodes, {len(other["edges"])} edges')

    # 5. cached semantic results for docs (no LLM calls here)
    docs = [f for cat in ('document', 'paper', 'image') for f in det['files'].get(cat, [])]
    sn, se, sh, uncached = check_semantic_cache(docs, root=str(ROOT), prompt_file=SPEC)
    print(f'semantic: {len(docs) - len(uncached)} cached docs, {len(uncached)} uncached (skipped)')
    if uncached:
        # Docs need the LLM extraction in the graphify skill (Part B); until then they are left out.
        print('  NOT IN GRAPH YET (run semantic extraction via /graphify for these):')
        for f in uncached[:20]:
            print(f'    {rel(f)}')

    nodes, ids = [], set()
    for n in other['nodes'] + list(c_nodes.values()) + sn:
        if n['id'] not in ids:
            ids.add(n['id'])
            nodes.append(n)
    edges, ekeys, dangling = [], set(), 0
    for e in other['edges'] + c_edges + se:
        if e['source'] not in ids or e['target'] not in ids:
            dangling += 1
            continue
        k = (e['source'], e['target'], e.get('relation'))
        if k not in ekeys:
            ekeys.add(k)
            edges.append(e)
    # declarations nobody uses would each become a one-node community; drop them
    degree = Counter()
    for e in edges:
        degree[e['source']] += 1
        degree[e['target']] += 1
    isolated_c = {i for i in c_nodes if degree[i] == 0}
    nodes = [n for n in nodes if n['id'] not in isolated_c]
    print(f'merged: {len(nodes)} nodes, {len(edges)} edges, {dangling} dangling edges dropped, '
          f'{len(isolated_c)} unused C declarations dropped')
    if args.dry_run:
        return

    # 6. build the graph the normal way
    extraction = {'nodes': nodes, 'edges': edges, 'hyperedges': sh,
                  'input_tokens': 0, 'output_tokens': 0}
    G = build_from_json(extraction, root=str(ROOT), directed=False)
    if G.number_of_nodes() == 0:
        sys.exit('ERROR: graph is empty')
    communities = cluster(G)
    cohesion = score_all(G, communities)
    gods = god_nodes(G)
    surprises = surprising_connections(G, communities)
    labels = {cid: ' / '.join(str(G.nodes[n].get('label', n))[:28]
                              for n in sorted(ns, key=lambda n: -G.degree(n))[:2])
              for cid, ns in communities.items()}
    questions = suggest_questions(G, communities, labels)
    graph_json = OUT / 'graph.json'
    prev = OUT / '.graph_prev.json'
    if graph_json.exists():
        graph_json.replace(prev)  # to_json refuses to shrink an existing graph (#479)
    to_json(G, communities, str(graph_json), community_labels=labels)
    report = generate(G, communities, cohesion, labels, gods, surprises, det,
                      {'input': 0, 'output': 0}, str(ROOT), suggested_questions=questions)
    (OUT / 'GRAPH_REPORT.md').write_text(report, encoding='utf-8')
    (OUT / '.graphify_labels.json').write_text(
        json.dumps({str(k): v for k, v in labels.items()}, ensure_ascii=False), encoding='utf-8')
    from graphify.cli import _stamped_manifest_files
    save_manifest(_stamped_manifest_files(det['files'], extraction, ROOT), root=str(ROOT),
                  scan_corpus={f for fl in det['files'].values() for f in fl})
    prev.unlink(missing_ok=True)
    print(f'graph: {G.number_of_nodes()} nodes, {G.number_of_edges()} edges, {len(communities)} communities')
    subprocess.run([sys.executable, '-m', 'graphify', 'export', 'html'], cwd=ROOT, check=False)


if __name__ == '__main__':
    main()
