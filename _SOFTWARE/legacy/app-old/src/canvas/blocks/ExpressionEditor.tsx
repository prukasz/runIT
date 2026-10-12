import { useEffect, useMemo, useRef, useState } from 'react'
import { AlertCircle, Calculator, Delete, Plus } from 'lucide-react'
import { compileExpression, decompileExpression, expressionLanguage, tokenize } from '../../domain/expression'
import type { ExpressionLanguage } from '../../domain/expression'
import type { VmBlockType } from '../../domain/descriptors'
import type { CanvasBlock, ObjectPath } from '../../domain/project'
import { pathText } from './blockView'

/*
 * The formula of an expression block (EXPR, EXPR_BIT): typed, or put together
 * on the keypad (touch). Checked as it is typed; applied on Enter, on leaving
 * the field, when the panel closes or the block is unselected, and only when it is valid. Stored as the RPN the
 * compiler takes; numbers become constants. At rest the inputs show as what
 * they are wired to; click to edit the text. Wired inputs are written by the
 * variable's name (an unwired one stays IN0, IN1 …); the stored formula keeps
 * the input numbers. A variable dropped
 * from the tree takes an empty input, or adds one, and lands in the formula.
 */

interface Props {
  readonly block: CanvasBlock
  readonly type: VmBlockType
  /** Input pins the block has now. */
  readonly inputCount: number
  readonly onApply: (expression: { constants: readonly number[]; code: readonly (string | number)[] }, inputsNeeded: number) => void
  /** A pin's path as the user reads it (names, not IDs). */
  readonly labelOf?: (path: ObjectPath) => string
  /** What a drag from the tree carries, when it is a variable. */
  readonly resolveDrop?: (data: DataTransfer) => { readonly pathText: string; readonly path?: ObjectPath } | undefined
  /** The object selected in the tree: a button puts it in the formula. */
  readonly selected?: { readonly name: string; readonly pathText: string; readonly path?: ObjectPath }
  /** Wire input `pin` to the variable, with the formula that now uses it when it is valid. */
  readonly onLink?: (link: { pin: number; pathText: string; path?: ObjectPath; inputsNeeded: number; expression?: { constants: readonly number[]; code: readonly (string | number)[] }; fromSelection?: boolean }) => void
}

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

/** A class per character, for the highlight layer (monospace: badges never change widths). */
const classify = (text: string, language: ExpressionLanguage): CharClass[] => {
  const classes: CharClass[] = Array.from(text, () => 'plain')
  const { tokens, error } = tokenize(text, language.flavor)
  for (const token of tokens) {
    let kind: CharClass = 'op'
    if (token.kind === 'input') kind = 'input'
    else if (token.kind === 'number') kind = 'number'
    else if (token.kind === 'open' || token.kind === 'close') kind = 'paren'
    else if (token.kind === 'name') {
      const name = token.text.toLowerCase()
      kind = language.functions.has(name) ? 'fn' : ['pi', 'e', 'true', 'false'].includes(name) ? 'number' : language.binary.some((entry) => entry.symbol === name) || language.unary[name] ? 'op' : 'bad'
    }
    for (let at = token.start; at < token.end; at++) classes[at] = kind
  }
  if (error) for (let at = error.start; at < text.length; at++) classes[at] = 'bad'
  return classes
}

export function ExpressionEditor({ block, type, inputCount, onApply, labelOf = pathText, resolveDrop, selected, onLink }: Props) {
  const language = useMemo(() => expressionLanguage(type.encoding!), [type])
  const stored = block.expression
  const storedText = useMemo(() => {
    if (!stored?.code.length) return ''
    return decompileExpression(stored.code, stored.constants ?? [], language)
  }, [stored, language])
  const [draft, setDraft] = useState(storedText ?? '')
  const [editing, setEditing] = useState(false)
  const [over, setOver] = useState<number | 'field' | undefined>()
  const [note, setNote] = useState('')
  const [pad, setPad] = useState(() => typeof window !== 'undefined' && window.matchMedia?.('(pointer: coarse)').matches)
  const [showAll, setShowAll] = useState(false)
  const inputRef = useRef<HTMLTextAreaElement>(null)

  /** How the text names input `pin`: its variable, when that reads as one word the formula cannot mistake for something else. */
  const aliases = new Map<number, string>()
  for (let pin = 0; pin < Math.max(inputCount, block.inputs?.length ?? 0); pin++) {
    const path = block.inputs?.[pin]
    const label = path && path.root !== '' ? labelOf(path) : ''
    const word = label.toLowerCase()
    if (label && !/\s/.test(label) && !/^in\d+$/.test(word) && !language.functions.has(word) && !['pi', 'e', 'true', 'false'].includes(word) && ![...aliases.values()].includes(label)) aliases.set(pin, label)
  }
  const aliasOf = (pin: number) => aliases.get(pin) ?? `IN${pin}`
  /** Text as shown (names) back to the formula (IN numbers). */
  const fromNamed = (text: string): string => {
    if (!aliases.size) return text
    const list = [...aliases.entries()].sort((left, right) => right[1].length - left[1].length)
    const pattern = new RegExp(`(?<![A-Za-z0-9_])(${list.map(([, alias]) => alias.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|')})(?![A-Za-z0-9_])`, 'g')
    return text.replace(pattern, (match) => `IN${list.find(([, alias]) => alias === match)![0]}`)
  }
  const named = (message: string) => message.replace(/\bIN(\d+)\b/g, (_, pin: string) => aliasOf(Number(pin)))

  const result = useMemo(() => (draft.trim() ? compileExpression(draft, language, type.inputs.max) : undefined), [draft, language, type.inputs.max])
  const changed = draft.trim() !== (storedText ?? '').trim()
  const error = result && !result.ok ? result.error : undefined
  const classes = useMemo(() => classify(draft, language), [draft, language])
  const inputsNeeded = result?.ok ? (result.compiled.inputs.at(-1) ?? -1) + 1 : 0
  const unused = result?.ok ? Array.from({ length: Math.max(inputCount, inputsNeeded) }, (_, pin) => pin).filter((pin) => !result.compiled.inputs.includes(pin)) : []

  const applied = useRef(storedText ?? '')
  const apply = () => {
    if (!result?.ok || !changed || draft.trim() === applied.current.trim()) return
    applied.current = draft
    onApply({ constants: result.compiled.constants, code: result.compiled.code }, inputsNeeded)
  }
  // Closing the panel or selecting another block leaves the editor: a valid formula is kept.
  const latestApply = useRef(apply)
  latestApply.current = apply
  useEffect(() => () => latestApply.current(), [])

  const wired = (pin: number) => !!block.inputs?.[pin] && block.inputs[pin]!.root !== ''
  /** Puts a variable in the formula: onto input `onto`, else an empty input, else a new one. */
  const link = (pathText: string, path: ObjectPath | undefined, onto?: number, fromSelection = false) => {
    if (!onLink) return
    setNote('')
    if (onto !== undefined) {
      onLink({ pin: onto, pathText, path, inputsNeeded: inputCount, fromSelection })
      return
    }
    const used = new Set(tokenize(draft, language.flavor).tokens.filter((token) => token.kind === 'input').map((token) => token.value))
    const span = Math.max(inputCount, inputsNeeded)
    let pin: number | undefined = Array.from({ length: inputCount }, (_, index) => index).find((index) => !wired(index) && !used.has(index))
    let add = true
    if (pin === undefined && span < type.inputs.max) pin = span
    if (pin === undefined) {
      pin = Array.from({ length: inputCount }, (_, index) => index).find((index) => !wired(index))
      add = false
    }
    if (pin === undefined) {
      setNote(`All ${type.inputs.max} inputs are wired: unwire one to use another variable.`)
      return
    }
    const field = inputRef.current
    const at = editing && field ? (field.selectionStart ?? shown.length) : shown.length
    const before = shown.slice(0, at)
    const after = shown.slice(at)
    const piece = `${before && !/[\s(,]$/.test(before) ? ' ' : ''}IN${pin}${after && !/^[\s),]/.test(after) ? ' ' : ''}`
    const next = add ? fromNamed(before + piece + after) : draft
    setDraft(next)
    const compiled = next.trim() ? compileExpression(next, language, type.inputs.max) : undefined
    const ok = compiled?.ok ? compiled.compiled : undefined
    onLink({
      pin,
      pathText,
      path,
      inputsNeeded: Math.max(pin + 1, inputCount, ok ? (ok.inputs.at(-1) ?? -1) + 1 : 0),
      ...(ok ? { expression: { constants: ok.constants, code: ok.code } } : {}),
      fromSelection,
    })
  }
  const dropHandlers = (onto?: number) => ({
    onDragOver: (event: React.DragEvent) => {
      if (!resolveDrop) return
      event.preventDefault()
      event.dataTransfer.dropEffect = 'copy'
      setOver(onto ?? 'field')
    },
    onDragLeave: () => setOver(undefined),
    onDrop: (event: React.DragEvent) => {
      setOver(undefined)
      const dropped = resolveDrop?.(event.dataTransfer)
      if (!dropped) return
      event.preventDefault()
      event.stopPropagation()
      link(dropped.pathText, dropped.path, onto)
    },
  })

  // Keypad editing keeps the field's caret and selection.
  const edit = (change: (text: string, start: number, end: number) => { text: string; caret: number }) => {
    const field = inputRef.current
    const start = field?.selectionStart ?? shown.length
    const end = field?.selectionEnd ?? shown.length
    const next = change(shown, start, end)
    setDraft(fromNamed(next.text))
    requestAnimationFrame(() => {
      field?.focus()
      field?.setSelectionRange(next.caret, next.caret)
    })
  }
  const insert = (piece: string) => edit((text, start, end) => {
    // No doubled spaces where a spaced operator meets existing space.
    let before = text.slice(0, start)
    let after = text.slice(end)
    let add = piece
    if (add.startsWith(' ') && (before.endsWith(' ') || !before)) add = add.trimStart()
    if (add.endsWith(' ') && after.startsWith(' ')) add = add.trimEnd()
    if (!before && add.startsWith(' ')) before = ''
    return { text: before + add + after, caret: before.length + add.length }
  })
  const backspace = () => edit((text, start, end) => {
    if (start !== end) return { text: text.slice(0, start) + text.slice(end), caret: start }
    // A whole input name or function name at once.
    const alias = [...aliases.values()].sort((left, right) => right.length - left.length).find((entry) => text.slice(0, start).endsWith(entry))
    const token = alias ? undefined : tokenize(text, language.flavor).tokens.find((entry) => entry.end === start && (entry.kind === 'input' || entry.kind === 'name'))
    let from = alias ? start - alias.length : token ? token.start : Math.max(0, start - 1)
    while (!alias && !token && from > 0 && text[from] === ' ' && text[from - 1] === ' ') from--
    return { text: text.slice(0, from) + text.slice(start), caret: from }
  })
  const move = (by: number) => {
    const field = inputRef.current
    if (!field) return
    const at = Math.max(0, Math.min(shown.length, (field.selectionStart ?? 0) + by))
    field.focus()
    field.setSelectionRange(at, at)
  }
  const press = (key: string) => {
    if (key === '⌫') backspace()
    else if (key === 'CLR') edit(() => ({ text: '', caret: 0 }))
    else if (key === '←') move(-1)
    else if (key === '→') move(1)
    else insert(PAD_TEXT[key] ?? key)
  }

  const functions = language.flavor === 'float' ? FLOAT_FUNCTIONS : BITS_FUNCTIONS
  const allFunctions = [...language.functions.keys()].filter((name) => !functions.includes(name) && !['and', 'or', 'xor', 'not', 'neg', 'mod', 'pow'].includes(name))
  const rows = language.flavor === 'float' ? FLOAT_PAD : BITS_PAD

  // What the field shows: the formula with each input written as its name.
  const segments: { text: string; kind: CharClass; error: boolean }[] = []
  const inputTokens = new Map(tokenize(draft, language.flavor).tokens.filter((token) => token.kind === 'input').map((token) => [token.start, token] as const))
  const inErrorAt = (at: number) => !!error && at >= error.start && at < Math.max(error.end, error.start + 1)
  for (let at = 0; at < draft.length;) {
    const token = inputTokens.get(at)
    const kind = token ? 'input' : classes[at] ?? 'plain'
    const inError = inErrorAt(at)
    const piece = token ? aliasOf(token.value!) : draft[at]!
    const last = segments.at(-1)
    if (last && last.kind === kind && last.error === inError) last.text += piece
    else segments.push({ text: piece, kind, error: inError })
    at = token ? token.end : at + 1
  }
  const shown = segments.map((segment) => segment.text).join('')

  return (
    <div className="expr-editor">
      <div className="expr-toolbar">
        <span className="expr-label">Formula</span>
        {selected && onLink && (
          <button type="button" className="expr-use-selected" title={`Use "${selected.name}" as an input of the formula`} onClick={() => link(selected.pathText, selected.path, undefined, true)}><Plus aria-hidden="true" />{selected.name}</button>
        )}
        <button type="button" className={`expr-pad-toggle ${pad ? 'selected' : ''}`} aria-pressed={pad} title={pad ? 'Hide the keypad' : 'Show the keypad'} onClick={() => setPad(!pad)}><Calculator aria-hidden="true" /></button>
      </div>

      {!editing ? (
        <div
          role="button"
          tabIndex={0}
          className={`expr-field expr-targets ${over === 'field' ? 'is-over' : ''}`}
          title="Click to edit the formula"
          onClick={() => setEditing(true)}
          onKeyDown={(event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); setEditing(true) } }}
          {...dropHandlers()}
        >
          {tokenize(draft, language.flavor).tokens.length === 0 && <span className="expr-placeholder">{resolveDrop ? 'Type a formula, or drop variables here' : 'No formula'}</span>}
          {(() => {
            const out: React.ReactNode[] = []
            let at = 0
            for (const token of tokenize(draft, language.flavor).tokens) {
              if (token.start > at) out.push(draft.slice(at, token.start))
              if (token.kind === 'input') {
                const pin = token.value!
                const path = block.inputs?.[pin]
                out.push(<span key={token.start} className={`expr-target ${path ? '' : 'is-unwired'} ${over === pin ? 'is-over' : ''}`} title={`IN${pin}${path ? '' : ': not wired, drop a variable here'}`} {...dropHandlers(pin)}>{path ? labelOf(path) : `IN${pin} ?`}</span>)
              } else out.push(<span key={token.start} className={`expr-tok-${classes[token.start]}`}>{token.text}</span>)
              at = token.end
            }
            if (at < draft.length) out.push(draft.slice(at))
            return out
          })()}
        </div>
      ) : (
        <div className={`expr-field ${over === 'field' ? 'is-over' : ''}`} {...dropHandlers()}>
          <pre className="expr-highlight" aria-hidden="true">
            {segments.map((segment, index) => <span key={index} className={`expr-tok-${segment.kind} ${segment.error ? 'expr-tok-error' : ''}`}>{segment.text}</span>)}
            {'\u200b'}
          </pre>
          <textarea
            ref={inputRef}
            className="expr-input"
            value={shown}
            rows={1}
            spellCheck={false}
            autoCapitalize="off"
            autoCorrect="off"
            autoFocus
            inputMode={pad ? 'none' : 'text'}
            aria-label="Formula"
            aria-invalid={!!error}
            placeholder={language.flavor === 'float' ? '(speed + 2) * angle' : '(flags << 4) | mask'}
            onChange={(event) => setDraft(fromNamed(event.target.value.replace(/\n/g, ' ')))}
            onKeyDown={(event) => {
              if (event.key === 'Enter') {
                event.preventDefault()
                apply()
                if (!draft.trim() || result?.ok) setEditing(false)
              } else if (event.key === 'Escape') {
                setDraft(storedText ?? '')
                setEditing(false)
              }
            }}
            onBlur={(event) => {
              if (event.currentTarget.closest('.expr-editor')?.contains(event.relatedTarget as Node | null)) return
              apply()
              // Back to the wired names once the formula stands; a broken one stays open with its error marked.
              if (!draft.trim() || result?.ok) setEditing(false)
            }}
          />
        </div>
      )}
      <div className={`expr-status ${error ? 'is-error' : ''}`} role="status">
        {error && <><AlertCircle aria-hidden="true" /><span>{named(error.message)}</span></>}
        {!draft.trim() && <span className="expr-muted">Use variable names and numbers{resolveDrop ? '; drop a variable from the tree to add it as an input.' : '.'}</span>}
      </div>
      {note && <p className="expr-note is-warning">{note}</p>}
      {result?.ok && inputsNeeded > inputCount && <p className="expr-note">Applying adds input{inputsNeeded - inputCount > 1 ? 's' : ''} {Array.from({ length: inputsNeeded - inputCount }, (_, index) => aliasOf(inputCount + index)).join(', ')}.</p>}
      {result?.ok && unused.length > 0 && <p className="expr-note">{unused.map((pin) => aliasOf(pin)).join(', ')} not in the formula: {unused.length > 1 ? 'they only trigger' : 'it only triggers'} the block.</p>}
      {stored?.code.length && storedText === undefined ? <p className="expr-note">The stored code is not a single formula: {stored.code.join(' ')}. Typing a formula replaces it.</p> : null}

      {pad && (
        <div className="expr-pad" onPointerDown={(event) => { if ((event.target as HTMLElement).closest('button')) event.preventDefault() }}>
          <div className="expr-pad-row is-inputs">
            {Array.from({ length: inputCount }, (_, pin) => (
              <button key={pin} type="button" className="expr-key is-input" title={block.inputs?.[pin] ? labelOf(block.inputs[pin]!) : `IN${pin}: not wired`} onClick={() => insert(aliasOf(pin))}>{aliasOf(pin)}</button>
            ))}
            {inputCount < type.inputs.max && <button type="button" className="expr-key is-input is-new" title="A new input" onClick={() => insert(`IN${Math.max(inputCount, inputsNeeded)}`)}>+ input</button>}
          </div>
          <div className="expr-pad-grid">
            {rows.map((row, rowIndex) => row.map((key) => (
              <button key={`${rowIndex}-${key}`} type="button" className={`expr-key ${/^[0-9A-F.]$/.test(key) ? 'is-digit' : ''} ${key === '⌫' || key === 'CLR' || key === '←' || key === '→' ? 'is-edit' : ''}`} aria-label={key === '⌫' ? 'Delete' : key === 'CLR' ? 'Clear' : undefined} onClick={() => press(key)}>
                {key === '⌫' ? <Delete aria-hidden="true" /> : PAD_LABEL[key] ?? key}
              </button>
            )))}
          </div>
          <div className="expr-pad-row is-functions">
            {functions.filter((name) => language.functions.has(name)).map((name) => (
              <button key={name} type="button" className="expr-key is-fn" title={language.functions.get(name)!.description} onClick={() => insert(`${name}(`)}>{name}</button>
            ))}
            <button type="button" className="expr-key is-more" aria-expanded={showAll} onClick={() => setShowAll(!showAll)}>{showAll ? 'less' : 'more…'}</button>
          </div>
          {showAll && (
            <div className="expr-pad-row is-functions">
              {allFunctions.map((name) => (
                <button key={name} type="button" className="expr-key is-fn" title={language.functions.get(name)!.description} onClick={() => insert(`${name}(`)}>{name}</button>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  )
}
