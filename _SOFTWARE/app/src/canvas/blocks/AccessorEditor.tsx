import { useMemo, useState } from 'react'
import { Folder, Plus, X } from 'lucide-react'
import { Badge } from '../../components/Badge'
import { Button, ToggleChip } from '../../components/Button'
import { MenuOption, OptionMenu } from '../../components/OptionMenu'
import { TypeBadge } from '../../components/TypeBadge/TypeBadge'
import type { ProjectDocument } from '../../domain/project'
import { arrayDims, formatChain, parseChain, resolveChain, rootCandidates, shapeText } from './accessorChain'
import type { ChainStep, RootCandidate } from './accessorChain'
import { resolveUniversalDrop } from './pinAccessors'
import type { UniversalDropInfo } from './pinAccessors'

const MAX_MEMBERS = 32
const MAX_ELEMENTS = 48
const MAX_CELLS = 8

/** A position: type a number or a variable, drop one from the tree, or click it in the tree and then here. */
function IndexStep({ text, candidates, selected, project, onChange, onRemove }: {
  text: string
  candidates: readonly RootCandidate[]
  selected?: UniversalDropInfo
  project?: ProjectDocument
  onChange: (text: string) => void
  onRemove: () => void
}) {
  const [over, setOver] = useState(false)
  const [focused, setFocused] = useState(false)
  const scalars = useMemo(() => candidates.filter((entry) => entry.node.kind === 'value' && entry.node.length === 1), [candidates])
  const query = text.trim().toLowerCase()
  const isNumber = /^\d*$/.test(query)
  const matches = useMemo(() => (query && !isNumber ? scalars.filter((entry) => entry.fullPath.toLowerCase().includes(query) && entry.fullPath.toLowerCase() !== query).slice(0, 6) : []), [scalars, query, isNumber])
  const known = scalars.find((entry) => entry.fullPath === text.trim() || entry.name === text.trim())
  const drop = (event: React.DragEvent) => {
    event.preventDefault()
    event.stopPropagation()
    setOver(false)
    const dropped = resolveUniversalDrop(event.dataTransfer, project)
    if (dropped) onChange(dropped.fullPath)
  }
  return (
    <span
      className={`accessor-step is-index ${text ? '' : 'is-empty'} ${over ? 'is-over' : ''} ${selected && !text ? 'can-click-to-assign' : ''}`}
      onDragOver={(event) => { event.preventDefault(); event.dataTransfer.dropEffect = 'copy'; setOver(true) }}
      onDragLeave={() => setOver(false)}
      onDrop={drop}
      onBlur={(event) => { if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setFocused(false) }}
      onClick={() => { if (selected && !text) onChange(selected.fullPath) }}
    >
      <span className="accessor-punct">[</span>
      {known?.node.kind === 'value' ? <TypeBadge type={known.node.type} /> : text && isNumber && <span className="pin-helpers-slot-key-tag">IDX</span>}
      <input
        className="accessor-input"
        value={text}
        size={Math.max(8, text.length + 1)}
        placeholder={selected ? `click for "${selected.name}"` : 'n or variable'}
        aria-label="Position: a number or a variable"
        onFocus={() => setFocused(true)}
        onChange={(event) => onChange(event.target.value)}
        onKeyDown={(event) => {
          if (event.key === 'ArrowDown' && matches.length) { event.preventDefault(); event.currentTarget.parentElement?.querySelector<HTMLButtonElement>('button[role="option"]:not(:disabled)')?.focus() }
          else if (event.key === 'Enter' && matches[0]) { event.preventDefault(); onChange(matches[0].fullPath) }
        }}
      />
      <span className="accessor-punct">]</span>
      <button type="button" className="pin-helpers-slot-clear" onClick={(event) => { event.stopPropagation(); onRemove() }} title="Remove this step" aria-label="Remove this step"><X aria-hidden="true" /></button>
      {focused && matches.length > 0 && (
        <OptionMenu className="pin-helpers-slot-dropdown" aria-label="Variable suggestions" onCancel={() => setFocused(false)}>
          {matches.map((entry) => (
            <MenuOption key={entry.id} className="pin-helpers-slot-option" keepInputFocus onChoose={() => onChange(entry.fullPath)}>
              <span>{entry.fullPath}</span>
              {entry.node.kind === 'value' && <TypeBadge type={entry.node.type} />}
            </MenuOption>
          ))}
        </OptionMenu>
      )}
    </span>
  )
}

/** A step typed in place: `.member`, `["key"]` or `[3]`. */
function TextStep({ step, onChange, onRemove }: { step: Extract<ChainStep, { kind: 'member' | 'key' }>; onChange: (step: ChainStep) => void; onRemove: () => void }) {
  const value = step.name
  const set = (next: string) => onChange({ kind: step.kind, name: next })
  return (
    <span className={`accessor-step is-${step.kind}`}>
      {step.kind === 'member' ? <span className="accessor-punct">.</span> : <span className="accessor-punct">[</span>}
      {step.kind === 'key' && <span className="pin-helpers-slot-key-tag">KEY</span>}
      <input className="accessor-input" value={value} size={Math.max(2, value.length + 1)} aria-label={step.kind === 'member' ? 'Member name' : 'Key'} onChange={(event) => set(event.target.value)} />
      {step.kind !== 'member' && <span className="accessor-punct">]</span>}
      <button type="button" className="pin-helpers-slot-clear" onClick={onRemove} title="Remove this step" aria-label="Remove this step"><X aria-hidden="true" /></button>
    </span>
  )
}

/**
 * A pin's variable as a chain of steps (`.member`, `["key"]`, `[3]`, `[variable]`) with what
 * can come next for the object reached, arrays of one, two or three dimensions included.
 */
export function AccessorEditor({ text, project, selected, onApply, onClearSelected }: {
  text: string
  project?: ProjectDocument
  /** The object selected in the tree: a click on a slot puts it there. */
  selected?: UniversalDropInfo
  onApply: (text: string) => void
  onClearSelected?: () => void
}) {
  const chain = useMemo(() => parseChain(text, project), [text, project])
  const candidates = useMemo(() => rootCandidates(project), [project])
  const result = useMemo(() => (project && chain.root ? resolveChain(project, chain) : undefined), [project, chain])
  const [by, setBy] = useState<'member' | 'key' | 'index'>('member')
  if (!chain.root || !project) return null

  const apply = (steps: readonly ChainStep[]) => onApply(formatChain({ root: chain.root, steps }))
  const replace = (at: number, step: ChainStep) => apply(chain.steps.map((entry, index) => (index === at ? step : entry)))
  const remove = (at: number) => apply(chain.steps.filter((_, index) => index !== at))
  const append = (...steps: ChainStep[]) => apply([...chain.steps, ...steps])
  const node = result?.node
  const folder = node?.kind === 'folder' ? node : undefined
  const dims = arrayDims(node, project)
  const takeSelected = () => { onClearSelected?.() }
  const firstName = folder?.children[0]?.name ?? 'name'

  return (
    <div className="accessor">
      <div
        className="accessor-chain"
        onDragOver={(event) => { event.preventDefault(); event.dataTransfer.dropEffect = 'copy' }}
        onDrop={(event) => {
          event.preventDefault()
          const dropped = resolveUniversalDrop(event.dataTransfer, project)
          if (dropped) append({ kind: 'index', text: dropped.fullPath })
        }}
      >
        <span className="accessor-root">{node?.kind === 'value' && chain.steps.length === 0 && <TypeBadge type={node.type} />}<strong>{chain.root}</strong></span>
        {chain.steps.map((step, index) => step.kind === 'index'
          ? <IndexStep key={index} text={step.text} candidates={candidates} selected={selected} project={project} onChange={(value) => { replace(index, { kind: 'index', text: value }); if (value && selected && value === selected.fullPath) takeSelected() }} onRemove={() => remove(index)} />
          : <TextStep key={index} step={step} onChange={(next) => replace(index, next)} onRemove={() => remove(index)} />)}
      </div>

      {result && !result.ok && result.reason && <div className="pin-helpers-warning">{result.reason}</div>}
      {result?.ok && node && (
        <div className="accessor-facts">
          {node.kind === 'value' && <TypeBadge type={node.type} />}
          {(dims || node.kind === 'folder') && <Badge>{shapeText(node, project)}</Badge>}
          {result.indexed && <Badge tone="success">element</Badge>}
        </div>
      )}

      {/* What can follow the object reached */}
      {result?.ok && folder && (
        <div className="pin-helpers-section">
          <div className="pin-helpers-label">
            <span><strong>{folder.name}</strong> holds {folder.children.length}:</span>
            <span className="accessor-by" role="group" aria-label="Select a member by">
              <ToggleChip selected={by === 'member'} onClick={() => setBy('member')} title="By name: .name">.name</ToggleChip>
              <ToggleChip selected={by === 'key'} onClick={() => setBy('key')} title='By key: ["name"]'>[&quot;key&quot;]</ToggleChip>
              <ToggleChip selected={by === 'index'} onClick={() => setBy('index')} title="By position: [n]">[n]</ToggleChip>
            </span>
          </div>
          {dims?.length === 2 ? (
            <div className="pin-helpers-matrix-grid accessor-cells" style={{ gridTemplateColumns: `repeat(${Math.min(dims[1]!, MAX_CELLS)}, minmax(0, 1fr))` }}>
              {Array.from({ length: Math.min(dims[0]!, MAX_CELLS) }, (_, row) => Array.from({ length: Math.min(dims[1]!, MAX_CELLS) }, (_, col) => (
                <button key={`${row}-${col}`} type="button" className="pin-helpers-elem-btn" title={`${chain.root}[${row}][${col}]`} onClick={() => append({ kind: 'index', text: String(row) }, { kind: 'index', text: String(col) })}>[{row}][{col}]</button>
              )))}
            </div>
          ) : (
            <div className="pin-helpers-chips">
              {folder.children.slice(0, MAX_MEMBERS).map((child, index) => (
                <button key={child.id} type="button" className="pin-helpers-chip" title={by === 'member' ? `.${child.name}` : by === 'key' ? `["${child.name}"]` : `[${index}]`} onClick={() => append(by === 'member' ? { kind: 'member', name: child.name } : by === 'key' ? { kind: 'key', name: child.name } : { kind: 'index', text: String(index) })}>
                  {child.kind === 'folder' && <Folder aria-hidden="true" size={12} />}
                  {by === 'index' ? `[${index}] ${child.name}` : child.name}
                </button>
              ))}
              {folder.children.length > MAX_MEMBERS && <span className="pin-helpers-more">+{folder.children.length - MAX_MEMBERS} more: type a name or position</span>}
            </div>
          )}
        </div>
      )}
      {result?.ok && node?.kind === 'value' && !result.indexed && node.length > 1 && node.type !== 'STR' && (
        <div className="pin-helpers-section">
          <div className="pin-helpers-label"><span><strong>{node.name}</strong> has {node.length} elements:</span></div>
          <div className="pin-helpers-elements-grid">
            {Array.from({ length: Math.min(node.length, MAX_ELEMENTS) }, (_, index) => (
              <button key={index} type="button" className="pin-helpers-elem-btn" onClick={() => append({ kind: 'index', text: String(index) })}>[{index}]</button>
            ))}
            {node.length > MAX_ELEMENTS && <span className="pin-helpers-more">+{node.length - MAX_ELEMENTS} more: use [n] or a variable</span>}
          </div>
        </div>
      )}

      <div className="accessor-add">
        <span className="accessor-add-label">Add step</span>
        <Button variant="dashed" size="sm" onClick={() => append({ kind: 'member', name: firstName })} title="A named member: .name"><Plus aria-hidden="true" />.name</Button>
        <Button variant="dashed" size="sm" onClick={() => append({ kind: 'key', name: firstName })} title='A named entry: ["name"]'><Plus aria-hidden="true" />[&quot;key&quot;]</Button>
        <Button variant="dashed" size="sm" onClick={() => { append({ kind: 'index', text: selected?.fullPath ?? '' }); if (selected) takeSelected() }} title={selected ? `A position read from "${selected.name}"` : 'A position: a number or a variable'}><Plus aria-hidden="true" />[n or variable]</Button>
      </div>
    </div>
  )
}
