import { useMemo, useRef, useState } from 'react'
import { AlertCircle, Calculator, Check, CornerDownLeft, Delete, Undo2 } from 'lucide-react'
import { compileExpression, decompileExpression, expressionLanguage, tokenize } from '../domain/expression'
import type { ExpressionLanguage } from '../domain/expression'
import type { VmBlockType } from '../domain/descriptors'
import type { CanvasBlock } from '../domain/project'
import { pathText } from './blockView'

/*
 * The formula of an expression block (EXPR, EXPR_BIT): typed, or put together
 * on the keypad (touch). Checked as it is typed; applied on Enter, on leaving
 * the field, or with Apply, and only when it is valid. Stored as the RPN the
 * compiler takes; numbers become constants. Inputs show as IN0, IN1 … badges,
 * or (Targets view) as what each input is wired to.
 */

interface Props {
  readonly block: CanvasBlock
  readonly type: VmBlockType
  /** Input pins the block has now. */
  readonly inputCount: number
  readonly onApply: (expression: { constants: readonly number[]; code: readonly (string | number)[] }, inputsNeeded: number) => void
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

export function ExpressionEditor({ block, type, inputCount, onApply }: Props) {
  const language = useMemo(() => expressionLanguage(type.encoding!), [type])
  const stored = block.expression
  const storedText = useMemo(() => {
    if (!stored?.code.length) return ''
    return decompileExpression(stored.code, stored.constants ?? [], language)
  }, [stored, language])
  const [draft, setDraft] = useState(storedText ?? '')
  const [view, setView] = useState<'pins' | 'targets'>('pins')
  const [pad, setPad] = useState(() => typeof window !== 'undefined' && window.matchMedia?.('(pointer: coarse)').matches)
  const [showAll, setShowAll] = useState(false)
  const inputRef = useRef<HTMLTextAreaElement>(null)

  const result = useMemo(() => (draft.trim() ? compileExpression(draft, language, type.inputs.max) : undefined), [draft, language, type.inputs.max])
  const changed = draft.trim() !== (storedText ?? '').trim()
  const error = result && !result.ok ? result.error : undefined
  const classes = useMemo(() => classify(draft, language), [draft, language])
  const inputsNeeded = result?.ok ? (result.compiled.inputs.at(-1) ?? -1) + 1 : 0
  const unused = result?.ok ? Array.from({ length: Math.max(inputCount, inputsNeeded) }, (_, pin) => pin).filter((pin) => !result.compiled.inputs.includes(pin)) : []

  const apply = () => {
    if (!result?.ok || !changed) return
    onApply({ constants: result.compiled.constants, code: result.compiled.code }, inputsNeeded)
  }

  // Keypad editing keeps the field's caret and selection.
  const edit = (change: (text: string, start: number, end: number) => { text: string; caret: number }) => {
    const field = inputRef.current
    const start = field?.selectionStart ?? draft.length
    const end = field?.selectionEnd ?? draft.length
    const next = change(draft, start, end)
    setDraft(next.text)
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
    // A whole input or name at once.
    const token = tokenize(text, language.flavor).tokens.find((entry) => entry.end === start && (entry.kind === 'input' || entry.kind === 'name'))
    let from = token ? token.start : Math.max(0, start - 1)
    while (!token && from > 0 && text[from] === ' ' && text[from - 1] === ' ') from--
    return { text: text.slice(0, from) + text.slice(start), caret: from }
  })
  const move = (by: number) => {
    const field = inputRef.current
    if (!field) return
    const at = Math.max(0, Math.min(draft.length, (field.selectionStart ?? 0) + by))
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

  const segments: { text: string; kind: CharClass; error: boolean }[] = []
  for (let at = 0; at < draft.length; at++) {
    const kind = classes[at] ?? 'plain'
    const inError = !!error && at >= error.start && at < Math.max(error.end, error.start + 1)
    const last = segments.at(-1)
    if (last && last.kind === kind && last.error === inError) last.text += draft[at]
    else segments.push({ text: draft[at]!, kind, error: inError })
  }

  return (
    <div className="expr-editor">
      <div className="expr-toolbar">
        <span className="expr-label">Formula</span>
        <div className="expr-view" role="radiogroup" aria-label="Show inputs as">
          <button type="button" role="radio" aria-checked={view === 'pins'} className={view === 'pins' ? 'selected' : ''} onClick={() => setView('pins')}>IN0</button>
          <button type="button" role="radio" aria-checked={view === 'targets'} className={view === 'targets' ? 'selected' : ''} onClick={() => setView('targets')}>Targets</button>
        </div>
        <button type="button" className={`expr-pad-toggle ${pad ? 'selected' : ''}`} aria-pressed={pad} title={pad ? 'Hide the keypad' : 'Show the keypad'} onClick={() => setPad(!pad)}><Calculator aria-hidden="true" /></button>
      </div>

      {view === 'targets' ? (
        <button type="button" className="expr-field expr-targets" title="Back to editing" onClick={() => setView('pins')}>
          {tokenize(draft, language.flavor).tokens.length === 0 && <span className="expr-placeholder">No formula</span>}
          {(() => {
            const out: React.ReactNode[] = []
            let at = 0
            for (const token of tokenize(draft, language.flavor).tokens) {
              if (token.start > at) out.push(draft.slice(at, token.start))
              if (token.kind === 'input') {
                const path = block.inputs?.[token.value!]
                out.push(<span key={token.start} className={`expr-target ${path ? '' : 'is-unwired'}`} title={`IN${token.value}`}>{path ? pathText(path) : `IN${token.value}: not wired`}</span>)
              } else out.push(<span key={token.start} className={`expr-tok-${classes[token.start]}`}>{token.text}</span>)
              at = token.end
            }
            if (at < draft.length) out.push(draft.slice(at))
            return out
          })()}
        </button>
      ) : (
        <div className="expr-field">
          <pre className="expr-highlight" aria-hidden="true">
            {segments.map((segment, index) => <span key={index} className={`expr-tok-${segment.kind} ${segment.error ? 'expr-tok-error' : ''}`}>{segment.text}</span>)}
            {'​'}
          </pre>
          <textarea
            ref={inputRef}
            className="expr-input"
            value={draft}
            rows={1}
            spellCheck={false}
            autoCapitalize="off"
            autoCorrect="off"
            inputMode={pad ? 'none' : 'text'}
            aria-label="Formula"
            aria-invalid={!!error}
            placeholder={language.flavor === 'float' ? '(IN0 + 2) * IN1' : '(IN0 << 4) | IN1'}
            onChange={(event) => setDraft(event.target.value.replace(/\n/g, ' '))}
            onKeyDown={(event) => {
              if (event.key === 'Enter') {
                event.preventDefault()
                apply()
              } else if (event.key === 'Escape') setDraft(storedText ?? '')
            }}
            onBlur={(event) => { if (!event.currentTarget.closest('.expr-editor')?.contains(event.relatedTarget as Node | null)) apply() }}
          />
        </div>
      )}

      <div className={`expr-status ${error ? 'is-error' : ''}`} role="status">
        {error && <><AlertCircle aria-hidden="true" /><span>{error.message}</span></>}
        {result?.ok && (
          <span>
            <Check aria-hidden="true" className="expr-ok" />
            RPN <code>{result.compiled.code.map((token, index, all) => (typeof token === 'number' ? null : token === 'in' ? `IN${all[index + 1]}` : token === 'const' ? String(result.compiled.constants[all[index + 1] as number]) : token)).filter((token) => token !== null).join(' ')}</code>
            <em> · stack {result.compiled.depth}/{language.encoding.stackMax} · {result.compiled.constants.length} const · {result.compiled.bytes} B</em>
          </span>
        )}
        {!draft.trim() && <span className="expr-muted">Type a formula; inputs are IN0 … IN{type.inputs.max - 1}, numbers become constants.</span>}
      </div>
      {result?.ok && inputsNeeded > inputCount && <p className="expr-note">Applying adds input{inputsNeeded - inputCount > 1 ? 's' : ''} {Array.from({ length: inputsNeeded - inputCount }, (_, index) => `IN${inputCount + index}`).join(', ')}.</p>}
      {result?.ok && unused.length > 0 && <p className="expr-note">{unused.map((pin) => `IN${pin}`).join(', ')} not in the formula: {unused.length > 1 ? 'they only trigger' : 'it only triggers'} the block.</p>}
      {changed && (
        <div className="expr-actions">
          <button type="button" className="expr-apply" disabled={!result?.ok} onClick={apply} title={result?.ok ? 'Apply (Enter)' : 'Fix the formula first'}><CornerDownLeft aria-hidden="true" />Apply</button>
          <button type="button" onClick={() => setDraft(storedText ?? '')} title="Back to the applied formula (Esc)"><Undo2 aria-hidden="true" />Revert</button>
          {!result?.ok && <span className="expr-muted">Not applied</span>}
        </div>
      )}
      {stored?.code.length && storedText === undefined ? <p className="expr-note">The stored code is not a single formula: {stored.code.join(' ')}. Typing a formula replaces it.</p> : null}

      {pad && (
        <div className="expr-pad" onPointerDown={(event) => { if ((event.target as HTMLElement).closest('button')) event.preventDefault() }}>
          <div className="expr-pad-row is-inputs">
            {Array.from({ length: inputCount }, (_, pin) => (
              <button key={pin} type="button" className="expr-key is-input" title={block.inputs?.[pin] ? pathText(block.inputs[pin]) : 'not wired'} onClick={() => insert(`IN${pin}`)}>IN{pin}</button>
            ))}
            {inputCount < type.inputs.max && <button type="button" className="expr-key is-input is-new" title="A new input" onClick={() => insert(`IN${Math.max(inputCount, inputsNeeded)}`)}>+IN</button>}
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
