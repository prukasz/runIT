import { useMemo, useState } from 'react'
import { Folder, Plus, X } from 'lucide-react'
import { appendStep, cutSteps, draftOf, EMPTY_DRAFT, formatDraft, inspectDraft, nextSteps, parseDraft, positionOptions, replaceStep, rootOptions, setRoot, shapeText, toPath } from '@engine'
import type { DraftStep, ObjectPath, PathDraft, ValueKind, ValueType, Variable } from '@engine'
import { Badge, Button, FieldError, MenuOption, OptionMenu, TextField, ToggleChip, TypeBadge, TypeLead, leadOf } from '@ui/components'
import { pathOf } from './variables'
import './PathEditor.css'

const MAX_CHILDREN = 32
const MAX_ELEMENTS = 48
const MAX_CELLS = 8

/** The lead square of what a pin carries. */
export const pinLead = (value: ValueKind) => (value === 'object' || value === 'cell' ? 'reference' : value === 'bool' || value === 'flow' ? 'bool' : 'number')

/** The type a new variable for a pin gets; none for a pin that takes a whole object. */
export const typeForPin = (value: ValueKind): ValueType | undefined =>
  value === 'bool' || value === 'flow' ? 'bool' : value === 'int' ? 'i32' : value === 'float' || value === 'any' ? 'f32' : undefined

/** A name a variable can have (the engine's rule for a plain path name). */
const isName = (text: string): boolean => /^[\p{L}_][\p{L}\p{N}_-]*$/u.test(text)

/** A position read at run time: an empty slot until a variable is chosen (typed, picked from the list, or the one selected in the tree). */
function SlotStep({ tree, path, selected, onChange, onRemove }: {
  tree: readonly Variable[]
  path: ObjectPath | null
  selected?: ObjectPath
  onChange: (path: ObjectPath | null) => void
  onRemove: () => void
}) {
  const [text, setText] = useState(path ? formatDraft(tree, draftOf(path)) : '')
  const [focused, setFocused] = useState(false)
  const options = useMemo(() => (text.trim() && !/^\d+$/.test(text.trim()) ? positionOptions(tree, text) : []), [tree, text])
  const commit = (next: string) => {
    setText(next)
    const read = parseDraft(tree, next)
    onChange(read.problem ? null : toPath(read.draft) ?? null)
  }
  return (
    <span className={`path-step is-slot ${path ? '' : 'is-empty'}`} onBlur={(event) => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setFocused(false) }}
      onClick={() => { if (!path && selected) commit(formatDraft(tree, draftOf(selected))) }}>
      <span className="path-punct">[</span>
      <TextField className="path-input" value={text} size={Math.max(8, text.length + 1)} placeholder={selected ? 'click: the selected one' : 'a variable'} aria-label="Position: a variable"
        onFocus={() => setFocused(true)} onChange={(event) => commit(event.target.value)}
        onKeyDown={(event) => { if (event.key === 'Enter' && options[0]) { event.preventDefault(); commit(options[0].text) } }} />
      <span className="path-punct">]</span>
      <button type="button" className="path-step-remove" title="Remove this step and those after it" aria-label="Remove this step" onClick={(event) => { event.stopPropagation(); onRemove() }}><X aria-hidden="true" /></button>
      {focused && options.length > 0 && (
        <OptionMenu className="path-suggestions" aria-label="Variables" onCancel={() => setFocused(false)}>
          {options.map((o) => (
            <MenuOption key={o.variable.id} keepInputFocus onChoose={() => commit(o.text)}>
              <span>{o.text}</span>
              {o.variable.kind === 'value' && <TypeBadge type={o.variable.type} />}
            </MenuOption>
          ))}
        </OptionMenu>
      )}
    </span>
  )
}

const stepText = (step: DraftStep): string => (step.kind === 'index' ? `[${step.index}]` : step.kind === 'name' ? `.${step.name}` : '')

/**
 * What a pin reads (or an output writes): a path into the variables, typed (`motor.gains[2]`, `table[position]`) or put together
 * from what the variable reached holds: its children by name or position, the elements of an array, a position read at run time.
 * What the path names, and why it names nothing, is the engine's walk (`pathBuilder`), the board's.
 */
export function PathEditor({ tree, title, value, path, selected, onChange, onCreate, onClose }: {
  tree: readonly Variable[]
  /** The pin's name. */
  title: string
  /** What the pin carries. */
  value: ValueKind
  path: ObjectPath | undefined
  /** The variable selected in the tree: one click uses it. */
  selected?: ObjectPath
  onChange: (path: ObjectPath | null) => void
  /** A new top-level variable for this pin; it returns the new variable's id. */
  onCreate?: (name: string, type: ValueType) => string | undefined
  onClose: () => void
}) {
  const [text, setText] = useState(path ? formatDraft(tree, draftOf(path)) : '')
  const [by, setBy] = useState<'name' | 'index'>('name')
  const read = useMemo(() => parseDraft(tree, text), [tree, text])
  const draft: PathDraft = read.draft
  const view = useMemo(() => inspectDraft(tree, draft), [tree, draft])
  const next = useMemo(() => nextSteps(tree, draft), [tree, draft])
  const typed = text.trim()
  const roots = useMemo(() => rootOptions(tree).filter((r) => !typed || r.variable.name.toLowerCase().includes(typed.split(/[.[]/)[0]!.toLowerCase())), [tree, typed])

  /** A draft edited by a button: the text follows it, and a complete one is the pin's path. */
  const apply = (next: PathDraft) => {
    const t = formatDraft(tree, next)
    setText(t)
    const complete = toPath(next)
    if (complete) onChange(complete)
  }
  const type = (t: string) => {
    setText(t)
    if (!t.trim()) return onChange(null)
    const r = parseDraft(tree, t)
    const complete = !r.problem && toPath(r.draft)
    if (complete) onChange(complete)
  }

  const resolution = view.resolution
  const reached = resolution?.ok ? resolution.target : undefined
  const newType = typeForPin(value)
  const rootName = typed.split(/[.[]/)[0] ?? ''
  const canCreate = !!onCreate && !!newType && draft.root === null && isName(rootName) && !tree.some((v) => v.name === rootName)
  const choosingRoot = draft.root === null || (draft.steps.length === 0 && !tree.some((v) => v.name === typed))

  return (
    <div className="path-editor" onMouseDown={(event) => {
      const tag = (event.target as HTMLElement).tagName
      if (tag !== 'BUTTON' && tag !== 'INPUT') event.preventDefault()
    }}>
      <div className="path-editor-head">
        <Badge tone="accent" caps filled>Pin</Badge>
        <strong>{title}</strong>
        <TypeLead kind={pinLead(value)} title={value} />
        <button type="button" className="path-editor-close" title="Close" aria-label="Close" onClick={onClose}><X aria-hidden="true" /></button>
      </div>

      {selected && (
        <div className="path-editor-selected">
          <span>Selected in the tree: <strong>{formatDraft(tree, draftOf(selected))}</strong></span>
          <Button size="sm" onClick={() => apply(draftOf(selected))}>Use it</Button>
        </div>
      )}

      <TextField className="path-editor-text" value={text} placeholder="A variable: motor.gains[2], table[position]" aria-label={`What ${title} reads`} autoFocus
        aria-invalid={!!read.problem || (resolution !== undefined && !resolution.ok)} onChange={(event) => type(event.target.value)} />
      <FieldError>{read.problem?.message ?? (resolution && !resolution.ok ? resolution.message : undefined)}</FieldError>

      {draft.root !== null && (
        <div className="path-chain">
          <span className="path-root"><strong>{pathOf(tree, draft.root) ?? draft.root}</strong></span>
          {draft.steps.map((step, i) => step.kind === 'dynamic'
            ? <SlotStep key={i} tree={tree} path={step.path} selected={selected} onChange={(p) => apply(replaceStep(draft, i, { kind: 'dynamic', path: p }))} onRemove={() => apply(cutSteps(draft, i))} />
            : (
              <span key={i} className="path-step">
                <span>{stepText(step)}</span>
                <button type="button" className="path-step-remove" title="Remove this step and those after it" aria-label="Remove this step" onClick={() => apply(cutSteps(draft, i))}><X aria-hidden="true" /></button>
              </span>
            ))}
          {reached && (
            <span className="path-facts">
              {reached.type && reached.type !== 'ptr' && <TypeBadge type={reached.type} count={reached.count > 1 ? reached.count : undefined} />}
              {reached.element !== null && <Badge tone="success">element</Badge>}
              {reached.dynamic && <Badge tone="info">read at run time</Badge>}
            </span>
          )}
        </div>
      )}

      {next.kind === 'children' && (
        <div className="path-section">
          <div className="path-label">
            <span><strong>{next.of.name}</strong> holds {next.children.length}:</span>
            <span className="path-by" role="group" aria-label="Pick by">
              <ToggleChip selected={by === 'name'} onClick={() => setBy('name')} title="By name: .name">.name</ToggleChip>
              <ToggleChip selected={by === 'index'} onClick={() => setBy('index')} title="By position: [n]">[n]</ToggleChip>
            </span>
          </div>
          {next.dimensions?.length === 2 ? (
            <div className="path-cells" style={{ gridTemplateColumns: `repeat(${Math.min(next.dimensions[1]!, MAX_CELLS)}, minmax(0, 1fr))` }}>
              {Array.from({ length: Math.min(next.dimensions[0]!, MAX_CELLS) }, (_, row) => Array.from({ length: Math.min(next.dimensions![1]!, MAX_CELLS) }, (_, col) => (
                <Button key={`${row}-${col}`} size="sm" onClick={() => apply(appendStep(draft, { kind: 'index', index: row }, { kind: 'index', index: col }))}>[{row}][{col}]</Button>
              )))}
            </div>
          ) : (
            <div className="path-chips">
              {next.children.slice(0, MAX_CHILDREN).map((c, i) => (
                <Button key={c.variable.id} size="sm" title={by === 'name' ? `.${c.variable.name}` : `[${i}]`} onClick={() => apply(appendStep(draft, by === 'name' ? c.byName : c.byIndex))}>
                  {c.variable.kind === 'folder' && <Folder aria-hidden="true" />}
                  {by === 'index' ? `[${i}] ${c.variable.name}` : c.variable.name}
                </Button>
              ))}
              {next.children.length > MAX_CHILDREN && <span className="path-more">+{next.children.length - MAX_CHILDREN} more: type a name or a position</span>}
            </div>
          )}
        </div>
      )}
      {next.kind === 'elements' && (
        <div className="path-section">
          <div className="path-label"><span><strong>{next.of.name}</strong> has {next.count} elements:</span></div>
          <div className="path-elements">
            {Array.from({ length: Math.min(next.count, MAX_ELEMENTS) }, (_, i) => (
              <Button key={i} size="sm" onClick={() => apply(appendStep(draft, { kind: 'index', index: i }))}>[{i}]</Button>
            ))}
            {next.count > MAX_ELEMENTS && <span className="path-more">+{next.count - MAX_ELEMENTS} more: type [n]</span>}
          </div>
        </div>
      )}
      {next.kind !== 'none' && (
        <div className="path-add">
          <Button variant="dashed" size="sm" title="A position read at run time from a variable" onClick={() => apply(appendStep(draft, { kind: 'dynamic', path: selected ?? null }))}><Plus aria-hidden="true" />[variable]</Button>
        </div>
      )}

      {choosingRoot && (
        <div className="path-section">
          <div className="path-label"><span>Variables ({roots.length}):</span></div>
          <OptionMenu className="path-roots" aria-label="Variables">
            {roots.slice(0, 8).map((r) => (
              <MenuOption key={r.variable.id} onChoose={() => apply(setRoot(EMPTY_DRAFT, r.variable.id))}>
                <TypeLead kind={r.variable.kind === 'value' ? leadOf(r.variable.type) : r.variable.kind} />
                <span className="path-root-name">{r.variable.name}{r.suffix}</span>
                {r.variable.kind === 'value' ? <TypeBadge type={r.variable.type} /> : <span className="path-more">{shapeText(tree, r.variable)}</span>}
              </MenuOption>
            ))}
            {roots.length === 0 && <span className="path-more">No variable matches.</span>}
          </OptionMenu>
        </div>
      )}
      {canCreate && (
        <div className="path-create">
          <span>There is no variable <strong>{rootName}</strong>.</span>
          <Button size="sm" variant="primary" onClick={() => {
            const id = onCreate!(rootName, newType!)
            if (id) {
              setText(rootName)
              onChange({ root: id, steps: [] })
            }
          }}><Plus aria-hidden="true" />Create {rootName} ({newType})</Button>
        </div>
      )}
    </div>
  )
}
