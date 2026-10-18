#!/usr/bin/env node
/*
 * Holds the app's screens to the shared UI parts (src/ui/components): a .tsx outside src/ui/components that writes raw markup a
 * component already draws (`<input>`, `<select>`, `className="badge …"`, a text button) is a finding. A line that must stay raw
 * says why with a `ui-raw: <reason>` comment on it or on the line above.
 *
 *   node tools/check-ui.mjs                 every .tsx under src/        (npm run check:ui; tests/ui/check-ui.test.ts runs it too)
 *   node tools/check-ui.mjs <file>…         those files
 *   node tools/check-ui.mjs --hook          a Claude Code PostToolUse hook: the edited file from the JSON on stdin; findings go to
 *                                           stderr with exit code 2, so the agent sees them and fixes the file
 */
import { readFileSync, readdirSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const SRC = join(ROOT, 'src')
const COMPONENTS = join(SRC, 'ui', 'components')

/** Raw elements a component replaces. */
const ELEMENTS = [
  [/<input\b/, 'TextField, NumberField, EditableField, Switch or PaletteSearch'],
  [/<select\b/, 'ListableField or ChoiceField'],
]

/** Class names a component owns (the first word of its CSS), and the component to use instead. */
const CLASSES = [
  [/^badge$/, 'Badge or TypeBadge'],
  [/^type-badge-group$|^object-type-icon$/, 'TypeBadge or TypeLead'],
  [/^ui-button$|^text-button$/, 'Button'],
  [/^ui-chip$/, 'ToggleChip'],
  [/^ui-card/, 'Card'],
  [/^tree-slab|^tree-children$/, 'TreeSlab, TreeDisclosure, TreeAction or TreeChildren'],
  [/^ref-pill/, 'RefPill'],
  [/^device-tile$/, 'DeviceTile'],
  [/^form-field-control$|^form-select-control$|^form-row$|^form-grid$/, 'TextField, SelectField, FormRow or FormGrid'],
  [/^adorned-field-wrap$|^editable-field|^listable-field|^dd(-|$)/, 'EditableField or ListableField'],
  [/^switch(-|$)/, 'Switch'],
  [/^segmented-switch$/, 'SegmentedSwitch'],
  [/^option-menu/, 'OptionMenu and MenuOption'],
  [/^palette-search/, 'PaletteSearch'],
  [/^palette-section/, 'PaletteSectionHeader'],
  [/^panel-header$/, 'PanelHeader'],
  [/^field-(label|note|error)/, 'FieldLabel or FieldError'],
  [/^number-field/, 'NumberField'],
]

const CLASS_ATTR = /className=(?:"([^"]*)"|'([^']*)'|\{`([^`]*)`\}|\{'([^']*)'\}|\{"([^"]*)"\})/g

/** The findings of one file: `{ line, text }`. */
export function checkSource(source) {
  const lines = source.split(/\r?\n/)
  const findings = []
  lines.forEach((line, index) => {
    if (/ui-raw:/.test(line) || /ui-raw:/.test(lines[index - 1] ?? '')) return
    for (const [pattern, use] of ELEMENTS) if (pattern.test(line)) findings.push({ line: index + 1, text: `${line.match(pattern)[0]}…> : use ${use}` })
    for (const match of line.matchAll(CLASS_ATTR)) {
      const words = (match.slice(1).find((group) => group !== undefined) ?? '').replace(/\$\{[^}]*\}/g, ' ').split(/\s+/).filter(Boolean)
      for (const word of words) {
        const rule = CLASSES.find(([pattern]) => pattern.test(word))
        if (rule) findings.push({ line: index + 1, text: `class "${word}": use ${rule[1]}` })
      }
    }
  })
  return findings
}

// Windows paths compare without case (a hook may get `c:\…` for `C:\…`).
const norm = (path) => (process.platform === 'win32' ? resolve(path).toLowerCase() : resolve(path))
const isChecked = (file) => file.endsWith('.tsx') && norm(file).startsWith(norm(SRC) + sep) && !norm(file).startsWith(norm(COMPONENTS) + sep)

const walk = (dir) => readdirSync(dir).flatMap((name) => {
  const path = join(dir, name)
  return statSync(path).isDirectory() ? walk(path) : [path]
})

/** The findings of `files` (every checked .tsx under src/ when none are given), each with its path relative to runit-app/. */
export function checkUi(files = walk(SRC)) {
  return files.filter(isChecked).flatMap((file) => checkSource(readFileSync(file, 'utf8')).map((finding) => ({ file: relative(ROOT, file).split(sep).join('/'), ...finding })))
}

const report = (findings) => findings.map(({ file, line, text }) => `${file}:${line}: ${text}`).join('\n')

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2)
  if (args[0] === '--hook') {
    let input = ''
    process.stdin.on('data', (chunk) => { input += chunk })
    process.stdin.on('end', () => {
      let file
      try { file = JSON.parse(input).tool_input?.file_path } catch { process.exit(0) }
      if (!file || !isChecked(file)) process.exit(0)
      const findings = checkUi([file])
      if (!findings.length) process.exit(0)
      process.stderr.write(`Use the shared UI parts (runit-app/src/ui/components, see runit-app/AGENTS.md "UI kit") instead of raw markup:\n${report(findings)}\nA line that must stay raw needs a "ui-raw: <reason>" comment.\n`)
      process.exit(2)
    })
  } else {
    const findings = checkUi(args.length ? args.map((arg) => resolve(arg)) : undefined)
    if (findings.length) {
      console.error(report(findings))
      process.exit(1)
    }
    console.log('check-ui: every screen uses the shared UI parts')
  }
}
