import { useEffect, useMemo, useRef, useState } from 'react'
import { AlertCircle, Calculator, Delete, Plus } from 'lucide-react'
import type { ExpressionLanguage } from '@engine'
import './ExpressionEditor.css'

/*
 * The formula of an expression block (`expr`, `expr_bit`): typed, or put together on the keypad (touch). Checked as it is typed
 * with the block's own language (the engine's: the opcodes of that block's machine); applied on Enter and on leaving the field,
 * and only when it is valid. What the block keeps is the text with its inputs as `in0`, `in1` …; the field writes a wired input
 * as what it reads (a variable's path, another block's output), so the user reads names. A variable dropped on the field, or
 * the one selected in the tree (its button, or a click in the field), takes a free input (or a new one) and lands in the formula
 * at the caret. A variable's name typed in the formula is found: the variables that fit the word at the caret are offered under
 * the field, and a name left in the formula becomes an input of that variable when the formula is applied.
 */

/** A variable the formula can name: its id, its own name (what is typed) and its path (what is shown). */
export interface FormulaVariable {
  readonly id: string
  readonly name: string
  readonly label: string
}

const WORD_BEFORE = /[\p{L}_][\p{L}\p{N}_]*$/u
const MAX_SUGGESTIONS = 6

type CharClass = 'input' | 'number' | 'fn' | 'op' | 'paren' | 'bad' | 'plain'

const FLOAT_PAD: readonly (readonly string[])[] = [
  ['7', '8', '9', '(', ')', '⌫'],
  ['4', '5', '6', '*', '/', '^'],
  ['1', '2', '3', '+', '-', '%'],
  ['0', '.', ',', '←', '→', 'CLR'],
  ['<', '>', '<=', '>=', '==', '!='],
  ['&&', '||', '!', '?', ':', 'pi'],
]
const FLOAT_FUNCTIONS = ['min', 'max', 'abs', 'sqrt', 'round', 'floor', 'sin', 'cos', 'atan2', 'hypot']
const BITS_PAD: readonly (readonly string[])[] = [
  ['7', '8', '9', '(', ')', '⌫'],
  ['4', '5', '6', '&', '|', '^'],
  ['1', '2', '3', '<<', '>>', '~'],
  ['0', '0x', ',', '←', '→', 'CLR'],
  ['A', 'B', 'C', 'D', 'E', 'F'],
]
const BITS_FUNCTIONS = ['bit', 'set', 'clear', 'toggle', 'popcount', 'rol', 'ror', 'clz', 'ctz', 'bswap']
/** Pad labels that insert something else. */
const PAD_TEXT: Readonly<Record<string, string>> = { '*': ' * ', '/': ' / ', '^': ' ^ ', '+': ' + ', '-': ' - ', '%': ' % ', '<': ' < ', '>': ' > ', '<=': ' <= ', '>=': ' >= ', '==': ' == ', '!=': ' != ', '&&': ' && ', '||': ' || ', '?': ' ? ', ':': ' : ', ',': ', ', '&': ' & ', '|': ' | ', '<<': ' << ', '>>': ' >> ' }
const PAD_LABEL: Readonly<Record<string, string>> = { '*': '×', '/': '÷', '-': '−', '&&': 'and', '||': 'or', '!': 'not', pi: 'π' }

const inputOf = (text: string): number => Number(text.slice(2))

export interface ExpressionEditorProps {
  readonly language: ExpressionLanguage
  /** The formula the block keeps, inputs as `in0`, `in1` … */
  readonly text: string
  /** Input pins the block has now, and the most it takes. */
  readonly inputCount: number
  readonly maxInputs: number
  /** What input `pin` reads, as the user names it; undefined while it is unwired. */
  readonly inputLabel: (pin: number) => string | undefined
  /** A valid formula to keep, and how many inputs it needs. */
  readonly onApply: (text: string, inputsNeeded: number) => void
  /** Something to put in the formula: the variable selected in the tree, or one dropped on the field (its name, for the button). */
  readonly selectedName?: string
  /** Wire input `pin` to the selected (or dropped) variable; `text` is the formula that now uses it (kept when valid). */
  readonly onLink?: (pin: number, text: string | undefined, inputsNeeded: number, dropped?: DataTransfer) => void
  /** Whether a drag carries a variable the field can take. */
  readonly accepts?: (data: DataTransfer) => boolean
  /** The variables a typed name can find. */
  readonly variables?: readonly FormulaVariable[]
  /** Wire these inputs to these variables (found by name); `text` is the formula that now uses them (kept when valid). */
  readonly onLinkVariables?: (links: readonly { readonly pin: number; readonly id: string }[], text: string | undefined, inputsNeeded: number) => void
}

export function ExpressionEditor({ language, text: stored, inputCount, maxInputs, inputLabel, onApply, selectedName, onLink, accepts, variables = [], onLinkVariables }: ExpressionEditorProps) {
  const [draft, setDraft] = useState(stored)
  const [editing, setEditing] = useState(false)
  const [over, setOver] = useState(false)
  const [note, setNote] = useState('')
  const [pad, setPad] = useState(() => typeof window !== 'undefined' && !!window.matchMedia?.('(pointer: coarse)').matches)
  const [showAll, setShowAll] = useState(false)
  /** Where the caret is in the shown text: the word before it is what the suggestions fit. */
  const [caret, setCaret] = useState(0)
  const field = useRef<HTMLTextAreaElement>(null)
  useEffect(() => { if (!editing) setDraft(stored) }, [stored, editing])

  // How the text names input `pin`: what it reads, when that is one word the formula cannot mistake for something else.
  const functionNames = useMemo(() => new Set(language.functions.map((f) => f.name.toLowerCase())), [language])
  const aliases = new Map<number, string>()
  for (let pin = 0; pin < inputCount; pin++) {
    const label = inputLabel(pin) ?? ''
    const word = label.toLowerCase()
    if (label && !/\s/.test(label) && !/^in\d+$/.test(word) && !functionNames.has(word) && !(word in language.constants) && ![...aliases.values()].includes(label)) aliases.set(pin, label)
  }
  const aliasOf = (pin: number) => aliases.get(pin) ?? `in${pin}`
  /** Text as shown (names) back to the formula (`in` numbers). */
  const fromNamed = (text: string): string => {
    if (!aliases.size) return text
    const list = [...aliases.entries()].sort((a, b) => b[1].length - a[1].length)
    const pattern = new RegExp(`(?<![\\p{L}\\p{N}_])(${list.map(([, alias]) => alias.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|')})(?![\\p{L}\\p{N}_\\[])`, 'gu')
    return text.replace(pattern, (match) => `in${list.find(([, alias]) => alias === match)![0]}`)
  }
  const named = (message: string) => message.replace(/\bin(\d+)\b/gi, (_, pin: string) => aliasOf(Number(pin)))

  const result = useMemo(() => (draft.trim() ? language.compile(draft, maxInputs) : undefined), [draft, language, maxInputs])
  const error = result && !result.ok ? result.error : undefined
  const tokens = useMemo(() => language.tokenize(draft), [draft, language])
  const inputsNeeded = result?.ok ? Math.max(-1, ...result.formula.inputs) + 1 : 0
  const unused = result?.ok ? Array.from({ length: Math.max(inputCount, inputsNeeded) }, (_, pin) => pin).filter((pin) => !result.formula.inputs.includes(pin)) : []
  // Only a variable the block reads counts: an unwired input not in the text is no one's.
  const wiredUnused = unused.filter((pin) => inputLabel(pin) !== undefined)

  const classOf = (kind: string, text: string): CharClass => {
    if (kind === 'input') return 'input'
    if (kind === 'number') return 'number'
    if (kind === 'open' || kind === 'close') return 'paren'
    if (kind === 'name') return functionNames.has(text.toLowerCase()) ? 'fn' : text.toLowerCase() in language.constants ? 'number' : 'bad'
    return 'op'
  }

  /** `count` inputs for new variables: the free ones first (unwired, not in `text`), then new ones; fewer when the block is full. */
  const freePins = (text: string, count: number): number[] => {
    const used = new Set(language.tokenize(text).tokens.filter((t) => t.kind === 'input').map((t) => inputOf(t.text)))
    const span = Math.max(inputCount, inputsNeeded, ...[...used].map((p) => p + 1))
    const free = Array.from({ length: inputCount }, (_, i) => i).filter((i) => inputLabel(i) === undefined && !used.has(i))
    for (let pin = span; free.length < count && pin < maxInputs; pin++) free.push(pin)
    return free.slice(0, count)
  }
  const neededBy = (text: string, at = 0): number => {
    const compiled = text.trim() ? language.compile(text, maxInputs) : undefined
    return Math.max(at, inputCount, compiled?.ok ? Math.max(-1, ...compiled.formula.inputs) + 1 : 0)
  }
  /** The variable a typed word names: by its path, else by its own name when only one variable has it. */
  const variableNamed = (word: string): FormulaVariable | undefined => {
    const lower = word.toLowerCase()
    const byLabel = variables.find((v) => v.label.toLowerCase() === lower)
    if (byLabel) return byLabel
    const byName = variables.filter((v) => v.name.toLowerCase() === lower)
    return byName.length === 1 ? byName[0] : undefined
  }
  /** The names in the formula that are variables, not functions or constants (draft positions). */
  const namedVariables = (text: string) => language.tokenize(text).tokens.flatMap((t) => {
    if (t.kind !== 'name' || functionNames.has(t.text.toLowerCase()) || t.text.toLowerCase() in language.constants) return []
    const variable = variableNamed(t.text)
    return variable ? [{ from: t.from, to: t.to, variable }] : []
  })

  const apply = () => {
    // Variables named in the formula become its inputs first: one input per variable, an input it is wired to already reused.
    const found = onLinkVariables ? namedVariables(draft) : []
    if (found.length) {
      const wired = new Map<string, number>()
      for (let pin = 0; pin < inputCount; pin++) {
        const label = inputLabel(pin)
        const variable = label === undefined ? undefined : variables.find((v) => v.label === label)
        if (variable && !wired.has(variable.id)) wired.set(variable.id, pin)
      }
      const fresh = [...new Set(found.map((f) => f.variable.id))].filter((id) => !wired.has(id))
      const pins = freePins(draft, fresh.length)
      if (pins.length < fresh.length) return setNote(`All ${maxInputs} inputs are in use: take one off to add another variable.`)
      const links = fresh.map((id, i) => ({ pin: pins[i]!, id }))
      for (const l of links) wired.set(l.id, l.pin)
      let next = draft
      for (const f of [...found].sort((a, b) => b.from - a.from)) next = `${next.slice(0, f.from)}in${wired.get(f.variable.id)}${next.slice(f.to)}`
      setDraft(next)
      const compiled = language.compile(next, maxInputs)
      onLinkVariables!(links, compiled.ok ? next : undefined, neededBy(next, Math.max(0, ...links.map((l) => l.pin + 1))))
      return
    }
    if (!result?.ok || draft.trim() === stored.trim()) return
    onApply(draft, Math.max(inputCount, inputsNeeded))
  }
  // Leaving the editor (another block selected, the panel closed) keeps a valid formula.
  const latestApply = useRef(apply)
  latestApply.current = apply
  useEffect(() => () => latestApply.current(), [])

  // What the field shows: the formula with each input written as its name, coloured by token.
  const segments: { text: string; kind: CharClass; error: boolean }[] = []
  const inErrorAt = (at: number) => !!error && at >= error.from && at < Math.max(error.to, error.from + 1)
  let cursor = 0
  const push = (text: string, kind: CharClass, err: boolean) => {
    const last = segments.at(-1)
    if (last && last.kind === kind && last.error === err) last.text += text
    else segments.push({ text, kind, error: err })
  }
  for (const token of tokens.tokens) {
    for (let at = cursor; at < token.from; at++) push(draft[at]!, 'plain', inErrorAt(at))
    push(token.kind === 'input' ? aliasOf(inputOf(token.text)) : token.text, classOf(token.kind, token.text), inErrorAt(token.from))
    cursor = token.to
  }
  for (let at = cursor; at < draft.length; at++) push(draft[at]!, tokens.error && at >= tokens.error.from ? 'bad' : 'plain', inErrorAt(at))
  const shown = segments.map((s) => s.text).join('')

  /** Puts a variable in the formula: a free input (unwired and not in the text), else a new one; at the caret. */
  const link = (dropped?: DataTransfer) => {
    if (!onLink) return
    setNote('')
    // An input the formula uses but nothing is wired to (a variable taken off it) is a hole: the variable fills it, the text stays.
    const hole = tokens.tokens.filter((t) => t.kind === 'input').map((t) => inputOf(t.text)).find((p) => inputLabel(p) === undefined)
    if (hole !== undefined) {
      onLink(hole, result?.ok ? draft : undefined, neededBy(draft, hole + 1), dropped)
      return
    }
    const pin: number | undefined = freePins(draft, 1)[0]
    if (pin === undefined) {
      setNote(`All ${maxInputs} inputs are in use: take one off to add another variable.`)
      return
    }
    const at = editing && field.current ? field.current.selectionStart ?? shown.length : shown.length
    const before = shown.slice(0, at)
    const after = shown.slice(at)
    const piece = `${before && !/[\s(,]$/.test(before) ? ' ' : ''}in${pin}${after && !/^[\s),]/.test(after) ? ' ' : ''}`
    const next = fromNamed(before) + piece + fromNamed(after)
    setDraft(next)
    const compiled = next.trim() ? language.compile(next, maxInputs) : undefined
    onLink(pin, compiled?.ok ? next : undefined, neededBy(next, pin + 1), dropped)
  }

  // The variables that fit the word before the caret (by name or path, the start first); none when it names one exactly.
  const fragment = editing ? WORD_BEFORE.exec(shown.slice(0, caret))?.[0] ?? '' : ''
  const lowerFragment = fragment.toLowerCase()
  const suggestions = !fragment || !onLinkVariables || /^in\d+$/i.test(fragment) || [...aliases.values()].includes(fragment) ? [] : variables
    .filter((v) => v.label.toLowerCase().includes(lowerFragment) || v.name.toLowerCase().includes(lowerFragment))
    .filter((v) => v.name.toLowerCase() !== lowerFragment || variables.filter((o) => o.name === v.name).length > 1)
    .sort((a, b) => Number(!a.name.toLowerCase().startsWith(lowerFragment)) - Number(!b.name.toLowerCase().startsWith(lowerFragment)) || a.label.localeCompare(b.label))
    .slice(0, MAX_SUGGESTIONS)
  /** The word before the caret becomes the chosen variable, as an input (its own, when one reads it already). */
  const choose = (variable: FormulaVariable) => {
    const before = shown.slice(0, caret - fragment.length)
    const after = shown.slice(caret)
    const wiredPin = Array.from({ length: inputCount }, (_, i) => i).find((i) => inputLabel(i) === variable.label)
    const pin = wiredPin ?? freePins(fromNamed(before + after), 1)[0]
    if (pin === undefined) return setNote(`All ${maxInputs} inputs are in use: take one off to add another variable.`)
    const next = `${fromNamed(before)}in${pin}${fromNamed(after)}`
    setDraft(next)
    const at = before.length + variable.label.length
    setCaret(at)
    requestAnimationFrame(() => {
      field.current?.focus()
      field.current?.setSelectionRange(at, at)
    })
    if (wiredPin === undefined) {
      const compiled = language.compile(next, maxInputs)
      onLinkVariables!([{ pin, id: variable.id }], compiled.ok ? next : undefined, neededBy(next, pin + 1))
    }
  }
  // A name the formula does not know but that is a variable: applying makes it an input, so it is no error to show.
  const pending = onLinkVariables ? namedVariables(draft) : []
  const pendingOnly = !!error && pending.some((p) => error.from >= p.from && error.from < p.to)
  const dropHandlers = {
    onDragOver: (event: React.DragEvent) => {
      if (!onLink || !accepts?.(event.dataTransfer)) return
      event.preventDefault()
      event.dataTransfer.dropEffect = 'copy'
      setOver(true)
    },
    onDragLeave: () => setOver(false),
    onDrop: (event: React.DragEvent) => {
      setOver(false)
      if (!onLink || !accepts?.(event.dataTransfer)) return
      event.preventDefault()
      event.stopPropagation()
      link(event.dataTransfer)
    },
  }

  // Keypad editing keeps the field's caret and selection.
  const edit = (change: (text: string, start: number, end: number) => { text: string; caret: number }) => {
    const el = field.current
    const next = change(shown, el?.selectionStart ?? shown.length, el?.selectionEnd ?? shown.length)
    setDraft(fromNamed(next.text))
    setEditing(true)
    requestAnimationFrame(() => {
      field.current?.focus()
      field.current?.setSelectionRange(next.caret, next.caret)
    })
  }
  const insert = (piece: string) => edit((text, start, end) => {
    const before = text.slice(0, start)
    const after = text.slice(end)
    let add = piece
    if (add.startsWith(' ') && (before.endsWith(' ') || !before)) add = add.trimStart()
    if (add.endsWith(' ') && after.startsWith(' ')) add = add.trimEnd()
    return { text: before + add + after, caret: before.length + add.length }
  })
  const backspace = () => edit((text, start, end) => {
    if (start !== end) return { text: text.slice(0, start) + text.slice(end), caret: start }
    // A whole input name or function name at once.
    const alias = [...aliases.values()].sort((a, b) => b.length - a.length).find((a) => text.slice(0, start).endsWith(a))
    const from = alias ? start - alias.length : Math.max(0, start - 1)
    return { text: text.slice(0, from) + text.slice(start), caret: from }
  })
  const move = (by: number) => {
    const el = field.current
    if (!el) return
    const at = Math.max(0, Math.min(shown.length, (el.selectionStart ?? 0) + by))
    el.focus()
    el.setSelectionRange(at, at)
  }
  const press = (key: string) => {
    if (key === '⌫') backspace()
    else if (key === 'CLR') edit(() => ({ text: '', caret: 0 }))
    else if (key === '←') move(-1)
    else if (key === '→') move(1)
    else insert(PAD_TEXT[key] ?? key)
  }

  const favourites = language.flavor === 'float' ? FLOAT_FUNCTIONS : BITS_FUNCTIONS
  const byName = new Map(language.functions.map((f) => [f.name.toLowerCase(), f]))
  const others = language.functions.filter((f) => !favourites.includes(f.name.toLowerCase()) && !language.operators.some((o) => o.op === f.op))
  const rows = language.flavor === 'float' ? FLOAT_PAD : BITS_PAD

  return (
    <div className="expr-editor">
      <div className="expr-toolbar">
        <span className="expr-label">Formula</span>
        {selectedName && onLink && (
          // ui-raw: a dashed chip that holds a variable's name, the formula's own look
          <button type="button" className="expr-use-selected" title={`Use ${selectedName} as an input of the formula`} onClick={() => link()}><Plus aria-hidden="true" />{selectedName}</button>
        )}
        <button type="button" className={`expr-pad-toggle ${pad ? 'selected' : ''}`} aria-pressed={pad} title={pad ? 'Hide the keypad' : 'Show the keypad'} aria-label="Keypad" onClick={() => setPad(!pad)}><Calculator aria-hidden="true" /></button>
      </div>

      {!editing ? (
        <div role="button" tabIndex={0} className={`expr-field expr-targets ${over ? 'is-over' : ''}`} title="Click to edit the formula"
          onClick={() => { if (selectedName && onLink) link(); setEditing(true) }} onKeyDown={(event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); setEditing(true) } }} {...dropHandlers}>
          {tokens.tokens.length === 0 && <span className="expr-placeholder">{onLink ? 'Type a formula, or drop variables here' : 'No formula'}</span>}
          {(() => {
            const out: React.ReactNode[] = []
            let at = 0
            for (const token of tokens.tokens) {
              if (token.from > at) out.push(draft.slice(at, token.from))
              if (token.kind === 'input') {
                const pin = inputOf(token.text)
                const label = inputLabel(pin)
                out.push(<span key={token.from} className={`expr-target ${label ? '' : 'is-unwired'}`} title={label ? `in${pin}: ${label}` : 'Not wired: drop a variable here, or pick one in the tree and click'}>{label ?? '?'}</span>)
              } else out.push(<span key={token.from} className={`expr-tok-${classOf(token.kind, token.text)}`}>{token.text}</span>)
              at = token.to
            }
            if (at < draft.length) out.push(draft.slice(at))
            return out
          })()}
        </div>
      ) : (
        <div className={`expr-field ${over ? 'is-over' : ''}`} {...dropHandlers}>
          <pre className="expr-highlight" aria-hidden="true">
            {segments.map((s, i) => <span key={i} className={`expr-tok-${s.kind} ${s.error ? 'expr-tok-error' : ''}`}>{s.text}</span>)}
            {'​'}
          </pre>
          {/* ui-raw: the transparent text layer over the coloured one; no kit field draws a highlight under its text */}
          <textarea ref={field} className="expr-input" value={shown} rows={1} spellCheck={false} autoCapitalize="off" autoCorrect="off" autoFocus inputMode={pad ? 'none' : 'text'}
            aria-label="Formula" aria-invalid={!!error} placeholder={language.flavor === 'float' ? '(speed + 2) * angle' : '(flags << 4) | mask'}
            onChange={(event) => { setDraft(fromNamed(event.target.value.replace(/\n/g, ' '))); setCaret(event.target.selectionStart ?? 0) }}
            onSelect={(event) => setCaret(event.currentTarget.selectionStart ?? 0)}
            onClick={() => { if (selectedName && onLink) link() }}
            onKeyDown={(event) => {
              if ((event.key === 'Tab' || event.key === 'Enter') && suggestions.length > 0) {
                event.preventDefault()
                choose(suggestions[0]!)
              } else if (event.key === 'Enter') {
                event.preventDefault()
                apply()
                if (!draft.trim() || (result?.ok && !pending.length)) setEditing(false)
              } else if (event.key === 'Escape') {
                setDraft(stored)
                setEditing(false)
              }
            }}
            onBlur={(event) => {
              if (event.currentTarget.closest('.expr-editor')?.contains(event.relatedTarget as Node | null)) return
              apply()
              // Back to the names once the formula stands (or its names became inputs); a broken one stays open with its error marked.
              if (!draft.trim() || result?.ok || pending.length) setEditing(false)
            }} />
        </div>
      )}
      {suggestions.length > 0 && (
        <div className="expr-suggestions" role="listbox" aria-label="Variables" onPointerDown={(event) => event.preventDefault()}>
          {suggestions.map((v, i) => (
            // ui-raw: a dashed chip that holds a variable's name, the formula's own look
            <button key={v.id} type="button" role="option" aria-selected={i === 0} className={`expr-suggestion ${i === 0 ? 'is-first' : ''}`} title={i === 0 ? `${v.label} (Tab or Enter)` : v.label} onClick={() => choose(v)}>{v.label}</button>
          ))}
        </div>
      )}
      <div className={`expr-status ${error && !pendingOnly ? 'is-error' : ''}`} role="status">
        {error && !pendingOnly && <><AlertCircle aria-hidden="true" /><span>{named(error.message)}</span></>}
        {pendingOnly && <span className="expr-muted">{[...new Set(pending.map((p) => p.variable.label))].join(', ')}: {pending.length > 1 ? 'variables, they become inputs' : 'a variable, it becomes an input'} on Enter or when you leave the field.</span>}
        {!draft.trim() && <span className="expr-muted">Type variables by their names, numbers and functions{onLink ? '; or drop a variable from the tree, or pick it there and click here.' : '.'}</span>}
      </div>
      {note && <p className="expr-note is-warning">{note}</p>}
      {result?.ok && wiredUnused.length > 0 && <p className="expr-note">{wiredUnused.map(aliasOf).join(', ')} not in the formula: {wiredUnused.length > 1 ? 'they only trigger' : 'it only triggers'} the block.</p>}

      {pad && (
        <div className="expr-pad" onPointerDown={(event) => { if ((event.target as HTMLElement).closest('button')) event.preventDefault() }}>
          {/* The variables the formula reads already, to use again; a new one comes by its name, a drop or a pick in the tree. */}
          {Array.from({ length: inputCount }, (_, pin) => pin).some((pin) => inputLabel(pin) !== undefined) && (
            <div className="expr-pad-row is-inputs">
              {Array.from({ length: inputCount }, (_, pin) => pin).filter((pin) => inputLabel(pin) !== undefined).map((pin) => (
                <button key={pin} type="button" className="expr-key is-input" title={inputLabel(pin)} onClick={() => insert(aliasOf(pin))}>{aliasOf(pin)}</button>
              ))}
            </div>
          )}
          <div className="expr-pad-grid">
            {rows.map((row, r) => row.map((key) => (
              <button key={`${r}-${key}`} type="button" className={`expr-key ${/^[0-9A-F.]$/.test(key) ? 'is-digit' : ''} ${['⌫', 'CLR', '←', '→'].includes(key) ? 'is-edit' : ''}`} aria-label={key === '⌫' ? 'Delete' : key === 'CLR' ? 'Clear' : undefined} onClick={() => press(key)}>
                {key === '⌫' ? <Delete aria-hidden="true" /> : PAD_LABEL[key] ?? key}
              </button>
            )))}
          </div>
          <div className="expr-pad-row is-functions">
            {favourites.filter((name) => byName.has(name)).map((name) => (
              <button key={name} type="button" className="expr-key is-fn" title={byName.get(name)!.description} onClick={() => insert(`${name}(`)}>{name}</button>
            ))}
            <button type="button" className="expr-key is-more" aria-expanded={showAll} onClick={() => setShowAll(!showAll)}>{showAll ? 'less' : 'more…'}</button>
          </div>
          {showAll && (
            <div className="expr-pad-row is-functions">
              {others.map((f) => <button key={f.name} type="button" className="expr-key is-fn" title={f.description} onClick={() => insert(`${f.name}(`)}>{f.name}</button>)}
            </div>
          )}
        </div>
      )}
    </div>
  )
}
