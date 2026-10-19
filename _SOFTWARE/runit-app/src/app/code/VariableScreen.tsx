import { useState } from 'react'
import type { ReactNode } from 'react'
import { ArrowDown, ArrowUp, Boxes, Link2 } from 'lucide-react'
import { NAME_MAX, VALUE_TYPES, findVariable, variableKey } from '@engine'
import type { ReferenceVariable, ValueType, ValueVariable, Variable } from '@engine'
import { Badge, Button, Card, CardStack, ChoiceField, FieldError, FormRow, NumberField, PanelHeader, RefPill, Switch, TextField, TreeAction, TreeDisclosure } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import { useLive } from '../run/live'
import type { CodeState } from './useCode'
import { TargetPill, VariableLead, useVariableErrors } from './VariableRow'
import { TYPE_LABEL, blockUses, folderChoices, initialText, isVariableTarget, maxCount, parseInitial, pathOf, retype, targetChoices } from './variables'
import type { BlockUse } from './variables'
import './Code.css'

const TYPE_CHOICES = VALUE_TYPES.map((t) => ({ value: t, label: TYPE_LABEL[t], description: t }))

/** The initial value: a true / false switch for a single boolean, else the elements as text (apart by commas). */
function InitialField({ variable, code }: { variable: ValueVariable; code: CodeState }) {
  // The text typed is kept while it does not parse, so the field never fights the fingers; the error says why.
  const [draft, setDraft] = useState<{ text: string; error?: string }>()
  if (variable.type === 'bool' && variable.count === 1) {
    const on = variable.initial === true || (Array.isArray(variable.initial) && variable.initial[0] === true)
    return (
      <div className="code-entry-initial is-switch">
        <Switch tone="truefalse" checked={on} label={on ? 'true' : 'false'} onChange={(checked) => code.update(variable.id, { initial: checked })} />
      </div>
    )
  }
  return (
    <div className="code-entry-initial">
      <TextField aria-label={`Initial value of ${variable.name}`} title="Initial value" value={draft?.text ?? initialText(variable)}
        placeholder={variable.type === 'str' ? 'Initial value: text' : variable.count > 1 ? 'Initial value: 0, 0, …' : 'Initial value: 0'}
        onChange={(event) => {
          const text = event.target.value
          const parsed = parseInitial(variable, text)
          if (parsed.ok) code.update(variable.id, { initial: parsed.initial }, `variable-initial:${variable.id}`)
          setDraft({ text, ...(parsed.ok ? {} : { error: parsed.error }) })
        }}
        onBlur={() => { if (!draft?.error) setDraft(undefined) }} />
      <FieldError>{draft?.error}</FieldError>
    </div>
  )
}

/** A reference's target: any variable that makes no cycle. */
function TargetField({ variable, code }: { variable: ReferenceVariable; code: CodeState }) {
  return (
    <ChoiceField<string> aria-label={`Target of ${variable.name}`} value={variable.target ?? undefined} choices={targetChoices(code.tree, variable.id)} placeholder="Points to…"
      unavailableLabel={() => 'Missing'} onChange={(target) => code.update(variable.id, { target: target ?? null })} />
  )
}

/**
 * The centre of the Code view (details): every variable as an entry to edit in place, folders with their entries inside. A click
 * on an entry selects it: the right panel shows the rest of it.
 */
export function VariablesEditor({ code }: { code: CodeState }) {
  const boardErrors = useVariableErrors()
  const live = useLive()
  const entries = (list: readonly Variable[]): ReactNode => list.map((v) => {
    const open = v.kind === 'folder' && !code.closed.has(v.id)
    const problems = code.problems.filter((p) => p.id === v.id)
    const classes = ['code-entry', `is-${v.kind}`, code.selected?.variable.id === v.id && 'is-selected', (problems.length > 0 || boardErrors.has(v.id)) && 'is-error'].filter(Boolean).join(' ')
    return (
      <div key={v.id} className="code-entry-wrap">
        <div className={classes} data-key={`variable:${v.id}`} onClick={() => code.select(v.id)}>
          <div className="code-entry-row">
            {v.kind === 'folder' && <TreeDisclosure open={open} onToggle={() => code.toggle(v.id)} />}
            <VariableLead variable={v} code={code} />
            <TextField className="code-entry-name" aria-label="Name" placeholder="Name" value={v.name} maxLength={NAME_MAX} onChange={(event) => code.update(v.id, { name: event.target.value }, `variable-name:${v.id}`)} />
            {v.kind === 'value' && (
              <span className="code-entry-type">
                <ChoiceField<ValueType> aria-label={`Type of ${v.name}`} value={v.type} choices={TYPE_CHOICES} onChange={(type) => type && type !== v.type && code.update(v.id, retype(v, type))} />
              </span>
            )}
            {v.kind === 'value' && live.text(variableKey(v.id)) !== undefined && <Badge tone="success" className={`live-value ${live.changed(variableKey(v.id)) ? 'is-changed' : ''}`} title="What the board holds now">{live.text(variableKey(v.id))}</Badge>}
            {v.kind === 'reference' && <span className="code-entry-target"><TargetField variable={v} code={code} /></span>}
            <span className="code-entry-delete"><TreeAction label={`Delete ${v.name}`} onClick={() => code.remove(v.id)} /></span>
          </div>
          {v.kind === 'value' && (
            <div className="code-entry-row is-value">
              <InitialField key={`${v.id}:${v.type}`} variable={v} code={code} />
              <span className="code-entry-count" title={v.type === 'str' ? 'Bytes' : 'Elements'}>
                <NumberField aria-label={`${v.type === 'str' ? 'Bytes' : 'Elements'} of ${v.name}`} integer min={1} max={maxCount(v.type)} value={v.count} onCommit={(count) => code.update(v.id, { count }, `variable-count:${v.id}`)} />
              </span>
              <span className="code-entry-icon" title={v.type === 'str' ? 'Bytes' : 'Elements'}><Boxes aria-hidden="true" /></span>
            </div>
          )}
          {problems.map((p, i) => <FieldError key={i}>{p.message}</FieldError>)}
        </div>
        {v.kind === 'folder' && open && v.children.length > 0 && <div className="code-entry-children">{entries(v.children)}</div>}
      </div>
    )
  })
  return (
    <div className="code-editor">
      {entries(code.tree)}
      {code.tree.length === 0 && <div className="view-placeholder"><p>No variables yet.</p></div>}
    </div>
  )
}

const ROLE = { input: (i: number) => `Input ${i + 1}`, output: (i: number) => `Output ${i + 1}`, enable: () => 'Run when' } as const

/**
 * The right panel of the Code view: the selected variable's place, flags, description, and what uses it (blocks, references), with
 * the board's errors about it.
 */
export function VariableDetails({ code }: { code: CodeState }) {
  const location = code.selected
  const variable = location?.variable
  if (!location || !variable || !findVariable(code.tree, variable.id)) return null
  const siblings = location.parent ? location.parent.children.length : code.tree.length
  const parent = location.parent?.id ?? null
  const uses: BlockUse[] = blockUses(code.program, variable.id)
  const pointing = (function walk(list: readonly Variable[]): Variable[] {
    return list.flatMap((v) => (v.kind === 'reference' && v.target === variable.id ? [v] : v.kind === 'folder' ? walk(v.children) : []))
  })(code.tree)

  return (
    <div className="code-details">
      <PanelHeader icon={<VariableLead variable={variable} code={code} />} title={pathOf(code.tree, variable.id) ?? variable.name} />
      <CardStack>
        <ErrorsCard match={(target) => isVariableTarget(target, variable.id)} />
        {variable.kind !== 'reference' && (
          <Card>
            <div className="code-switches">
              <Switch checked={variable.subscribed ?? false} label="Subscribe" onChange={(subscribed) => code.update(variable.id, { subscribed })} />
              {variable.kind === 'value' && <>
              <Switch checked={variable.retained ?? false} label="Keep after restart" onChange={(retained) => code.update(variable.id, { retained })} />
              <Switch checked={variable.protected ?? false} label="Read only for the app" onChange={(on) => code.update(variable.id, { protected: on })} />
              <Switch checked={variable.writable === false} label="Constant" onChange={(constant) => code.update(variable.id, { writable: !constant })} />
              </>}
            </div>
          </Card>
        )}
        {variable.kind === 'reference' && (
          <Card title="Points to">
            <TargetField variable={variable} code={code} />
            {variable.target !== null && <div className="code-refs"><TargetPill variable={variable} code={code} /></div>}
          </Card>
        )}
        {(variable.kind === 'folder' || variable.kind === 'reference') && (
          <Card title="Contents">
            <FormRow label="Known at run time" note={variable.kind === 'folder'
              ? 'What it holds is built while the program runs (a block re-links its entries, a parsed message): a path below it may name what the project does not hold, and the board looks it up by name or position.'
              : 'What it points to is built while the program runs (a clone of a message): a path below it may name what the project does not hold, and the board looks it up; [0] is what it points to.'}>
              <Switch checked={variable.dynamic ?? false} label={variable.dynamic ? 'Checked on the board' : 'Checked here'} onChange={(dynamic) => code.update(variable.id, { dynamic: dynamic || undefined })} />
            </FormRow>
          </Card>
        )}
        <Card title="Place">
          <div className="code-place">
            <ChoiceField<string> aria-label="Folder" value={location.parent?.id} choices={folderChoices(code.tree, variable.id)} emptyLabel="Top level"
              onChange={(folder) => code.move(variable.id, folder ?? null)} />
            <Button size="sm" aria-label="Move up" title="Move up" disabled={location.index === 0} onClick={() => code.move(variable.id, parent, location.index - 1)}><ArrowUp aria-hidden="true" /></Button>
            <Button size="sm" aria-label="Move down" title="Move down" disabled={location.index >= siblings - 1} onClick={() => code.move(variable.id, parent, location.index + 1)}><ArrowDown aria-hidden="true" /></Button>
          </div>
          <TextField aria-label="Description" placeholder="Description" value={variable.description ?? ''}
            onChange={(event) => code.update(variable.id, { description: event.target.value || undefined }, `variable-description:${variable.id}`)} />
        </Card>
        {(uses.length > 0 || pointing.length > 0) && (
          <Card title="Used by">
            {uses.length > 0 && (
              <FormRow label="Blocks">
                <div className="code-refs">
                  {uses.map((u) => <RefPill key={`${u.block}:${u.role}:${u.index}`} compact icon={Boxes} name={code.blockLabel(u.block)} detail={ROLE[u.role](u.index)} refKey={`block:${u.block}`} />)}
                </div>
              </FormRow>
            )}
            {pointing.length > 0 && (
              <FormRow label="References">
                <div className="code-refs">{pointing.map((r) => <RefPill key={r.id} compact icon={Link2} name={pathOf(code.tree, r.id) ?? r.name} onJump={() => code.select(r.id)} />)}</div>
              </FormRow>
            )}
          </Card>
        )}
      </CardStack>
    </div>
  )
}
