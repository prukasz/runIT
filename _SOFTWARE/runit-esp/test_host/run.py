#!/usr/bin/env python3
"""Builds and runs the host tests of the firmware's components (HOST_TESTS.MD).

    python test_host/run.py                 # every component that has host/host_test.json
    python test_host/run.py errors router   # only these
    python test_host/run.py --list
    python test_host/run.py errors -k wire  # only the test files whose path contains "wire"

A component's host/host_test.json says what to build:
    sources   its .c files (relative to the component folder) built together with the tests
    tests     the test files (each has a main() with Unity: it is one executable)
    includes  more include folders of the component (relative to it), when its CMakeLists has more than "."
    config    a header with the component's CONFIG_* defaults (becomes part of the generated sdkconfig.h)
    deps      other components whose sources (and config) are built in too
    variants  name -> extra compiler flags; the tests run once per variant
Only the platform is replaced (test_host/include, test_host/src); the component's code is the real one.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent          # runit-esp/test_host
FW = ROOT.parent                                # runit-esp
BUILD = FW / "build_host"
UNITY = ROOT / "vendor" / "unity"
CFLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-g", "-pthread"]


def rel(path):
    """A path relative to runit-esp, with forward slashes (the compiler runs there: short __FILE__ in the output)."""
    return Path(path).resolve().relative_to(FW).as_posix()


def discover():
    found = {}
    for manifest in sorted((FW / "components").glob("*/*/host/host_test.json")):
        comp = manifest.parent.parent
        found[comp.name] = (comp, json.loads(manifest.read_text(encoding="utf-8")))
    return found


def closure(name, comps, seen=None):
    """The component and, first, its deps (each once)."""
    seen = [] if seen is None else seen
    if name not in comps:
        sys.exit(f"error: no host tests for '{name}' (known: {', '.join(comps)})")
    for dep in comps[name][1].get("deps", []):
        if dep not in seen:
            closure(dep, comps, seen)
    if name not in seen:
        seen.append(name)
    return seen


def build_and_run(name, comps, variant, flags, test_filter):
    comp, manifest = comps[name]
    parts = closure(name, comps)
    out = BUILD / name / variant
    out.mkdir(parents=True, exist_ok=True)

    # sdkconfig.h: the config headers of the component and its deps
    cfg = ["#pragma once"]
    for p in parts:
        if comps[p][1].get("config"):
            cfg.append(f'#include "{(comps[p][0] / comps[p][1]["config"]).as_posix()}"')
    (out / "sdkconfig.h").write_text("\n".join(cfg) + "\n", encoding="utf-8")

    incs = [out, ROOT / "include", UNITY]
    for p in parts:  # the component folder and the include folders its CMakeLists names beyond it
        incs.append(comps[p][0])
        incs += [comps[p][0] / d for d in comps[p][1].get("includes", [])]
    incs = [rel(i) for i in incs]
    srcs = [ROOT / "src" / "host_mocks.c", ROOT / "src" / "host_freertos.c", UNITY / "unity.c"]
    for p in parts:
        srcs += [comps[p][0] / s for s in comps[p][1]["sources"]]

    ok = True
    for test in manifest["tests"]:
        if test_filter and test_filter not in test:
            continue
        exe = out / (Path(test).stem + ".exe")
        cmd = ["gcc", *CFLAGS, *flags, *[f"-I{i}" for i in incs], *map(rel, srcs), rel(comp / test), "-o", rel(exe)]
        print(f"== {name} [{variant}] {test}", flush=True)
        if subprocess.run(cmd, cwd=FW).returncode != 0:
            print("   BUILD FAILED")
            ok = False
            continue
        if subprocess.run([str(exe)], cwd=FW).returncode != 0:
            ok = False
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("components", nargs="*")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-k", dest="test_filter", default="")
    args = ap.parse_args()

    comps = discover()
    if args.list:
        for n, (c, m) in comps.items():
            print(f"{n:20} {', '.join(m['tests'])}  variants: {', '.join(m.get('variants', {'default': []}))}")
        return 0
    names = args.components or list(comps)
    failed = []
    for n in names:
        closure(n, comps)  # validates the name
        for variant, flags in comps[n][1].get("variants", {"default": []}).items():
            if not build_and_run(n, comps, variant, flags, args.test_filter):
                failed.append(f"{n}[{variant}]")
    print("\nFAILED: " + ", ".join(failed) if failed else "\nall host tests passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
