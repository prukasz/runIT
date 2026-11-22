import type { ReactNode } from 'react'
import { Download, Plus, RefreshCw, Send, Trash2 } from 'lucide-react'
import { bundledEnums } from '@backend'
import type { FieldValues, WireField } from '@backend'
import { Badge, Button, Card, CardStack, ChoiceField, FieldError, FormGrid, FormRow, NumberField, Switch, TextField } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import type { Choice } from '@ui/components'
import type { GattCharacteristic, GattService } from '../project/projectFile'
import { FieldsForm } from '../devices/FieldsForm'
import type { PinContext } from '../devices/FieldsForm'
import { ownGattService } from './gatt'
import { settingTitle } from './settings'
import type { SettingGroup, SettingSection, SettingTable } from './settings'
import type { BoardSettingsState, SettingSource } from './useBoardSettings'

/*
 * A tab of the Settings view: the settings of one service descriptor. Every label, unit, choice and help text is the
 * descriptor's (the write command's fields); the values are the project's, or the board's while the project keeps none.
 */

/** Where the values come from, and the two ways to move them: Apply now (to the board) and Use the board's (out of the project). */
function SourceBar({ source, differs, kept, connected, busy, onApply, onDrop }: {
  source: SettingSource; differs: boolean; kept: boolean; connected: boolean; busy: boolean; onApply: () => void; onDrop: () => void
}) {
  return (
    <>
      {source === 'project' && (differs
        ? <Badge tone="warning" title="The project's values are not what the board has now">not on the board</Badge>
        : <Badge tone="success" title={kept ? 'Kept in the project: written at Write, applied at every start' : 'What the board has'}>{kept ? 'in the project' : 'on the board'}</Badge>)}
      {source === 'board' && <Badge tone="info" title="The project keeps none: a change here puts it in the project">from the board</Badge>}
      {connected && (differs || (!kept && source === 'project')) && (
        <Button size="sm" variant="primary" disabled={busy} title="Send these values to the board now: it keeps them until it restarts. Write (the toolbar) keeps them for good" onClick={onApply}><Send aria-hidden="true" />Apply now</Button>
      )}
      {source === 'project' && (
        <Button size="sm" title={kept ? "Drop these values from the project: the board keeps its own" : 'Drop the changes'} onClick={onDrop}><Download aria-hidden="true" />{kept ? "Use the board's" : 'Revert'}</Button>
      )}
    </>
  )
}

function FormCard({ group, state, context }: { group: SettingGroup; state: BoardSettingsState; context: PinContext }) {
  const kept = group.recordId !== undefined
  const source = kept ? state.formSource(group) : state.formDiffers(group) ? 'project' : state.formSource(group)
  return (
    <Card title={settingTitle(group.command)} help={group.command.write?.description ?? group.command.description}
      actions={<span className="settings-actions"><SourceBar source={source} differs={state.formDiffers(group)} kept={kept} connected={state.connected} busy={state.busy !== undefined}
        onApply={() => state.applyForm(group)} onDrop={() => state.dropForm(group)} /></span>}>
      {source === 'none' && state.connected && <p className="project-note">Reading the board…</p>}
      <FieldsForm layout={group.layout} values={state.formValues(group)} context={context} disabled={!kept && !state.connected}
        onChange={(values: FieldValues) => state.changeForm(group, values)} />
      <FieldError>{state.error(group.key)}</FieldError>
    </Card>
  )
}

/** The members of the enum a column index names, the `…_NONE` one (no event) left out. */
const columnsOf = (field: WireField): { readonly index: number; readonly label: string; readonly description?: string }[] =>
  (bundledEnums()[field.indexEnum ?? '']?.members ?? [])
    .filter((m) => !m.name.endsWith('_NONE'))
    .map((m) => ({ index: m.value, label: m.alias ?? m.name, ...(m.description ? { description: m.description } : {}) }))

/** A settings table, one column per target and one row per event kind: each cell a choice of the enum the cells take. */
function TableCard({ table, state, nameOf }: { table: SettingTable; state: BoardSettingsState; nameOf: (device: number) => string }) {
  const rows = state.tableRowsOf(table)
  const columns = columnsOf(table.cells)
  const choices: Choice<number>[] = (table.cells.choices ?? []).map((c) => ({ value: Number(c.value), label: c.label, ...(c.description ? { description: c.description } : {}) }))
  const set = (row: number, index: number, value: number) =>
    state.changeTable(table, rows.map((r, i) => (i === row ? { ...r, cells: r.cells.map((c, j) => (j === index ? value : c)) } : r)))
  return (
    <Card title={settingTitle(table.command)} help={table.command.write?.description ?? table.command.description}
      actions={<span className="settings-actions"><SourceBar source={state.tableSource(table)} differs={state.tableDiffers(table)} kept connected={state.connected} busy={state.busy !== undefined}
        onApply={() => state.applyTable(table)} onDrop={() => state.dropTable(table)} /></span>}>
      {rows.length === 0
        ? <p className="project-note">{state.connected ? 'Reading the board…' : 'Connect the board: its rows are its rails.'}</p>
        : (
          <table className="settings-table">
            <thead><tr><th />{rows.map((r) => <th key={r.target}>{nameOf(r.target)}</th>)}</tr></thead>
            <tbody>
              {columns.map((col) => (
                <tr key={col.index} data-help={col.description}>
                  <th>{col.label}</th>
                  {rows.map((r, i) => (
                    <td key={r.target}>
                      <ChoiceField<number> aria-label={`${col.label}: ${nameOf(r.target)}`} value={r.cells[col.index]} choices={choices} onChange={(v) => v !== undefined && set(i, col.index, v)} />
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        )}
      <FieldError>{state.error(table.key)}</FieldError>
    </Card>
  )
}

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(4, '0')}`
const parseHex = (text: string): number | undefined => {
  const n = Number(text.trim().startsWith('0x') ? text.trim() : `0x${text.trim()}`)
  return Number.isInteger(n) && n > 0 && n <= 0xffff ? n : undefined
}

/** A 16-bit UUID as hex text; `optional`: empty is 0 (none). */
function UuidField({ label, value, optional, onChange }: { label: string; value: number; optional?: boolean; onChange: (value: number) => void }) {
  return (
    <TextField aria-label={label} defaultValue={optional && value === 0 ? '' : hex(value)} key={value} onBlur={(event) => {
      if (optional && event.target.value.trim() === '') return onChange(0)
      const n = parseHex(event.target.value)
      if (n !== undefined) onChange(n)
    }} />
  )
}

function GattEditor({ section, state }: { section: SettingSection; state: BoardSettingsState }) {
  const services = state.gattServices()
  const own = ownGattService()
  const board = state.live.gatt ?? (own ? [own] : [])
  const taken = new Set([
    ...board.flatMap((s) => [s.uuid, ...s.characteristics.map((c) => c.uuid)]),
    ...services.flatMap((s) => [s.uuid, ...s.characteristics.flatMap((c) => [c.tx, c.rx])]),
  ])
  const free = (from: number) => { let n = from; while (taken.has(n)) n++; return n }
  // A new provider's id: a number no provider of the project has, above the board's own.
  const freeProvider = () => {
    const used = new Set(services.flatMap((s) => s.characteristics.map((c) => c.provider)))
    let n = 0x10
    while (used.has(n)) n++
    return n
  }
  const set = (next: readonly GattService[]) => state.changeGatt(next)
  const setService = (i: number, s: GattService) => set(services.map((o, j) => (j === i ? s : o)))
  const setChar = (i: number, k: number, change: (c: GattCharacteristic) => GattCharacteristic) =>
    setService(i, { ...services[i]!, characteristics: services[i]!.characteristics.map((o, j) => (j === k ? change(o) : o)) })
  return (
    <>
      <Card title="Bluetooth" help={section.service.description}
        actions={<span className="settings-actions"><SourceBar source={state.gattSource()} differs={state.gattDiffers()} kept connected={state.connected} busy={state.busy !== undefined}
          onApply={() => state.applyGatt(section)} onDrop={() => state.dropGatt()} /></span>}>
        <p className="project-note">A way is a provider of the board: the characteristic its frames go out on (notify) and the one they come in from (written). The first way of a service makes the service. Apply now builds them at once: the board drops the Bluetooth link for a moment and the app connects again. They are lost at a restart unless Write (the toolbar) stores them with the project.</p>
        <FieldError>{state.error(section.service.id)}</FieldError>
      </Card>
      {board.map((s) => (
        <Card key={s.uuid} title={`${hex(s.uuid)} · ${s.locked ? "the board's" : 'on the board now'}`} help={s.locked ? "The board's own service: the app's link. It cannot be changed." : 'A service the board has now.'}>
          <ul className="settings-list">{s.characteristics.map((c) => <li key={c.uuid}><code>{hex(c.uuid)}</code> {c.name}</li>)}</ul>
        </Card>
      ))}
      {services.map((s, i) => (
        <Card key={`${i}:${s.uuid}`} title={`Service ${hex(s.uuid)}`}
          actions={<Button size="sm" title="Remove this service with its ways" onClick={() => set(services.filter((_, j) => j !== i))}><Trash2 aria-hidden="true" />Remove</Button>}>
          <FormGrid>
            <FormRow label="UUID" note="The 16-bit UUID of the service, shared by its ways."><UuidField label="Service UUID" value={s.uuid} onChange={(uuid) => setService(i, { ...s, uuid })} /></FormRow>
          </FormGrid>
          {s.characteristics.map((c, k) => (
            <div key={`${k}:${c.provider}`} className="settings-char">
              <div className="settings-group-head">
                <FormRow label="Provider" note="The id of this way: what the data matrix routes to and from.">
                  <NumberField aria-label="Provider id" integer min={1} max={254} value={c.provider} onCommit={(provider) => setChar(i, k, (o) => ({ ...o, provider }))} />
                </FormRow>
                <Button size="sm" title="Remove this way" onClick={() => setService(i, { ...s, characteristics: s.characteristics.filter((_, j) => j !== k) })}><Trash2 aria-hidden="true" />Remove</Button>
              </div>
              <FormGrid>
                <FormRow label="Out (notify)" note="The characteristic its frames go out on; empty: none."><UuidField label="Out UUID" value={c.tx} optional onChange={(tx) => setChar(i, k, (o) => ({ ...o, tx }))} /></FormRow>
                <FormRow label="In (written)" note="The characteristic the client writes; empty: none."><UuidField label="In UUID" value={c.rx} optional onChange={(rx) => setChar(i, k, (o) => ({ ...o, rx }))} /></FormRow>
                <FormRow label="Frames" note="Both carry frames (a session's way); off: raw bytes (a stream's).">
                  <Switch label={c.framed ? 'On' : 'Off'} checked={c.framed} onChange={(framed) => setChar(i, k, (o) => ({ ...o, framed }))} />
                </FormRow>
                <FormRow label="Name" note="The user description of the characteristics (up to 15 characters).">
                  <TextField aria-label="Way name" maxLength={15} value={c.name} onChange={(event) => setChar(i, k, (o) => ({ ...o, name: event.target.value }))} />
                </FormRow>
              </FormGrid>
              {c.tx === 0 && c.rx === 0 && <FieldError>{`Provider ${c.provider}: give it an out or an in UUID.`}</FieldError>}
            </div>
          ))}
          <div className="project-actions">
            <Button size="sm" onClick={() => setService(i, { ...s, characteristics: [...s.characteristics, { provider: freeProvider(), tx: free(s.uuid + 1), rx: 0, framed: false, name: '' }] })}>
              <Plus aria-hidden="true" />A way
            </Button>
          </div>
        </Card>
      ))}
      <div className="project-actions">
        <Button onClick={() => set([...services, { uuid: free(0xee00), characteristics: [{ provider: freeProvider(), tx: free(0xee01), rx: 0, framed: false, name: '' }] }])}><Plus aria-hidden="true" />A service</Button>
      </div>
    </>
  )
}

/** The forms of a section's settings (a tab's cards, or the Project tab's board settings). */
export function SettingsCards({ section, state, context, nameOf }: { section: SettingSection; state: BoardSettingsState; context: PinContext; nameOf: (device: number) => string }): ReactNode {
  return (
    <>
      {section.groups.map((g) => <FormCard key={g.key} group={g} state={state} context={context} />)}
      {section.tables.map((t) => <TableCard key={t.key} table={t} state={state} nameOf={nameOf} />)}
      {section.record?.editor === 'ble' && <GattEditor section={section} state={state} />}
    </>
  )
}

/** A settings tab: Read while connected, and its cards (its name is the tab's, in the left panel). */
export function SettingsScreen({ section, state, context, nameOf }: { section: SettingSection; state: BoardSettingsState; context: PinContext; nameOf: (device: number) => string }) {
  return (
    <div className="project-screen">
      {state.connected && (
        <div className="project-actions">
          <Button size="sm" title="Read these settings from the board: the forms show what it has (Undo brings the project's back)" onClick={() => state.read(section)}><RefreshCw aria-hidden="true" />Read</Button>
        </div>
      )}
      <CardStack>
        {section.service.key !== undefined && <ErrorsCard about={`router_key:${section.service.key}`} />}
        <SettingsCards section={section} state={state} context={context} nameOf={nameOf} />
      </CardStack>
    </div>
  )
}
