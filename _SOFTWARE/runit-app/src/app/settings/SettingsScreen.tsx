import type { ReactNode } from 'react'
import { Download, Plus, RefreshCw, Send, Trash2 } from 'lucide-react'
import { bundledEnums } from '@backend'
import type { FieldValues, WireField } from '@backend'
import { Badge, Button, Card, CardStack, ChoiceField, FieldError, FormGrid, FormRow, PanelHeader, Switch, TextField } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import type { Choice } from '@ui/components'
import type { DataStream, GattCharacteristic, GattService } from '../project/projectFile'
import { FieldsForm } from '../devices/FieldsForm'
import type { PinContext } from '../devices/FieldsForm'
import { GATT } from './gatt'
import { settingTitle } from './settings'
import type { SettingGroup, SettingSection, SettingTable } from './settings'
import { STREAMS, transports } from './streams'
import type { BoardSettingsState, SettingSource } from './useBoardSettings'

/*
 * A tab of the Project view: the settings of one service descriptor. Every label, unit, choice and help text is the
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
      {source === 'none'
        ? <p className="project-note">{state.connected ? 'Reading the board…' : 'Connect the board to read it, or set it here.'}</p>
        : null}
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

/** A 16-bit UUID as hex text. */
function UuidField({ label, value, onChange }: { label: string; value: number; onChange: (value: number) => void }) {
  return <TextField aria-label={label} defaultValue={hex(value)} key={value} onBlur={(event) => { const n = parseHex(event.target.value); if (n !== undefined) onChange(n) }} />
}

function GattEditor({ section, state, context }: { section: SettingSection; state: BoardSettingsState; context: PinContext }) {
  const catalog = state.sectionCatalog
  const entry = catalog.service(GATT.service)
  const svcFields = catalog.command(entry, GATT.createService).write!.args!
  const charLayout = catalog.command(entry, GATT.createChar).write!.args!
  const label = (layout: typeof svcFields, id: string) => layout.fields.find((f) => f.id === id)?.label ?? id
  const services = state.gattServices()
  const locked = (state.live.gatt ?? []).filter((s) => s.locked)
  const taken = new Set([...locked, ...services].flatMap((s) => [s.uuid, ...s.characteristics.map((c) => c.uuid)]))
  const free = (from: number) => { let n = from; while (taken.has(n)) n++; return n }
  const set = (next: readonly GattService[]) => state.changeGatt(next)
  const setService = (i: number, s: GattService) => set(services.map((o, j) => (j === i ? s : o)))
  const charValues = (c: GattCharacteristic): FieldValues => ({ is_write: c.write, is_indicate: c.indicate, is_notify: c.notify, is_framed: c.framed, tx_buffer_size: c.tx, rx_buffer_size: c.rx, desc: c.name })
  // A buffer only exists for a direction the characteristic has (the board refuses a TX buffer without notify / indicate and an RX buffer without write):
  // turning a direction on gives it a usual size, turning it off drops it.
  const USUAL = 128
  const fromValues = (c: GattCharacteristic, v: FieldValues): GattCharacteristic => {
    const [write, indicate, notify] = [Boolean(v.is_write), Boolean(v.is_indicate), Boolean(v.is_notify)]
    const out = notify || indicate
    const [tx, rx] = [Number(v.tx_buffer_size ?? 0), Number(v.rx_buffer_size ?? 0)]
    return {
      ...c, write, indicate, notify, framed: Boolean(v.is_framed), name: String(v.desc ?? ''),
      tx: !out ? 0 : tx === 0 && !(c.notify || c.indicate) ? USUAL : tx,
      rx: !write ? 0 : rx === 0 && !c.write ? USUAL : rx,
    }
  }
  return (
    <>
      <Card title="Bluetooth services" help={section.service.description}
        actions={<span className="settings-actions"><SourceBar source={state.gattSource()} differs={state.gattDiffers()} kept connected={state.connected} busy={state.busy !== undefined}
          onApply={() => state.applyGatt(section)} onDrop={() => state.dropGatt()} /></span>}>
        <p className="project-note">Apply now rebuilds the table at once: the board drops the Bluetooth link for a moment and the app connects again. The table is lost at a restart unless Write (the toolbar) stores it with the project.</p>
        <FieldError>{state.error(section.service.id)}</FieldError>
      </Card>
      {locked.map((s) => (
        <Card key={s.uuid} title={`${hex(s.uuid)} · the board's`} help="The board's own service: the app's link. It cannot be changed.">
          <ul className="settings-list">{s.characteristics.map((c) => <li key={c.uuid}><code>{hex(c.uuid)}</code> {c.name}</li>)}</ul>
        </Card>
      ))}
      {services.map((s, i) => (
        <Card key={`${i}:${s.uuid}`} title={`Service ${hex(s.uuid)}`} help={catalog.command(entry, GATT.createService).description}
          actions={<Button size="sm" title="Remove this service" onClick={() => set(services.filter((_, j) => j !== i))}><Trash2 aria-hidden="true" />Remove</Button>}>
          <FormGrid>
            <FormRow label={label(svcFields, 'uuid')} note={svcFields.fields.find((f) => f.id === 'uuid')?.description}><UuidField label={label(svcFields, 'uuid')} value={s.uuid} onChange={(uuid) => setService(i, { ...s, uuid })} /></FormRow>
            <FormRow label={label(svcFields, 'is_primary')} note={svcFields.fields.find((f) => f.id === 'is_primary')?.description}>
              <Switch label={s.primary ? 'On' : 'Off'} checked={s.primary} onChange={(primary) => setService(i, { ...s, primary })} />
            </FormRow>
          </FormGrid>
          {s.characteristics.map((c, k) => (
            <div key={`${k}:${c.uuid}`} className="settings-char">
              <div className="settings-group-head">
                <FormRow label={label(charLayout, 'uuid')} note={charLayout.fields.find((f) => f.id === 'uuid')?.description}>
                  <UuidField label={label(charLayout, 'uuid')} value={c.uuid} onChange={(uuid) => setService(i, { ...s, characteristics: s.characteristics.map((o, j) => (j === k ? { ...o, uuid } : o)) })} />
                </FormRow>
                <Button size="sm" title="Remove this characteristic" onClick={() => setService(i, { ...s, characteristics: s.characteristics.filter((_, j) => j !== k) })}><Trash2 aria-hidden="true" />Remove</Button>
              </div>
              <FieldsForm layout={charLayout} values={charValues(c)} context={context} hide={(f) => f.id === 'svc_uuid' || f.id === 'uuid' || (f.id === 'tx_buffer_size' && !(c.notify || c.indicate)) || (f.id === 'rx_buffer_size' && !c.write)}
                onChange={(v) => setService(i, { ...s, characteristics: s.characteristics.map((o, j) => (j === k ? fromValues(o, v) : o)) })} />
            </div>
          ))}
          <div className="project-actions">
            <Button size="sm" onClick={() => setService(i, { ...s, characteristics: [...s.characteristics, { uuid: free(s.uuid + 1), name: '', write: false, indicate: false, notify: true, framed: false, tx: 128, rx: 0 }] })}>
              <Plus aria-hidden="true" />{catalog.command(entry, GATT.createChar).label}
            </Button>
          </div>
        </Card>
      ))}
      <div className="project-actions">
        <Button onClick={() => set([...services, { uuid: free(0xee00), primary: true, characteristics: [] }])}><Plus aria-hidden="true" />{catalog.command(entry, GATT.createService).label}</Button>
      </div>
    </>
  )
}

function StreamsEditor({ section, state, context }: { section: SettingSection; state: BoardSettingsState; context: PinContext }) {
  const catalog = state.sectionCatalog
  const entry = catalog.service(STREAMS.service)
  const create = catalog.command(entry, STREAMS.create).write!.args!
  const bind = catalog.command(entry, STREAMS.bind)
  const streams = state.streamsOf()
  // The board's four are named by the group `connector`; the project's by its own label.
  const streamNames = new Map((bundledEnums().connector?.members ?? []).map((m) => [m.value, m.alias ?? m.name]))
  const byteName = (stream: number) => `Stream 0x${stream.toString(16).toUpperCase().padStart(2, '0')}`
  const title = (s: DataStream) => (s.system ? streamNames.get(s.stream) : s.name) || byteName(s.stream)
  // A stream is sent on a characteristic that notifies or indicates: the board's and the project's.
  const chars = [...(state.live.gatt ?? []).filter((s) => s.locked), ...state.gattServices()].flatMap((s) => s.characteristics).filter((c) => c.notify || c.indicate)
  const uuidNames = new Map((bundledEnums().ble_runit_uuid?.members ?? []).map((m) => [m.value, m.alias ?? m.name]))
  const charChoices: Choice<number>[] = chars.map((c) => ({ value: c.uuid, label: `${hex(c.uuid)} ${uuidNames.get(c.uuid) ?? c.name}`.trim() }))
  const set = (next: readonly DataStream[]) => state.changeStreams(next)
  const setStream = (at: number, change: (s: DataStream) => DataStream) => set(streams.map((s, i) => (i === at ? change(s) : s)))
  const bleTransport = transports().find((t) => /ble/i.test(bundledEnums().transport_e?.members.find((m) => m.value === t.value)?.name ?? ''))?.value
  return (
    <>
      <Card title={section.title} help={section.service.description}
        actions={<span className="settings-actions"><SourceBar source={state.streamsSource()} differs={state.streamsDiffer()} kept connected={state.connected} busy={state.busy !== undefined}
          onApply={() => state.applyStreams(section)} onDrop={() => state.dropStreams()} /></span>}>
        <FieldError>{state.error(section.service.id)}</FieldError>
      </Card>
      {streams.map((s, at) => (
        <Card key={at} title={title(s)} help={s.system ? "One of the board's own streams: it can be sent elsewhere or paused, not removed." : undefined}
          actions={(
            <span className="settings-actions">
              {s.system ? <Badge tone="system">the board's</Badge> : <Button size="sm" title="Remove this stream" onClick={() => set(streams.filter((_, i) => i !== at))}><Trash2 aria-hidden="true" />Remove</Button>}
            </span>
          )}>
          <FormGrid>
            {s.system
              ? <FormRow label={create.fields.find((f) => f.id === 'connector')?.label ?? 'Connector'}><span>{streamNames.get(s.stream) ?? s.stream}</span></FormRow>
              : <>
                  <FormRow label="Name" note="The project's own label: the board keeps no name.">
                    <TextField value={s.name ?? ''} placeholder={byteName(s.stream)} aria-label={`${title(s)}: name`} onChange={(e) => setStream(at, (o) => ({ ...o, name: e.currentTarget.value }))} />
                  </FormRow>
                  <FieldsForm grid={false} layout={create} values={{ connector: s.stream }} context={context}
                    onChange={(v) => setStream(at, (o) => ({ ...o, stream: Number(v.connector ?? o.stream) }))} />
                </>}
            <FormRow label={catalog.command(entry, STREAMS.suspend).label} note={catalog.command(entry, STREAMS.suspend).description}>
              <Switch label={s.suspended ? 'Paused' : 'Sending'} checked={s.suspended} onChange={(suspended) => setStream(at, (o) => ({ ...o, suspended }))} />
            </FormRow>
          </FormGrid>
          {transports().map((t) => {
            const binding = s.bindings.find((b) => b.provider === t.value)
            const last = s.system && s.bindings.length === 1 && binding !== undefined
            return (
              <FormGrid key={t.value}>
                <FormRow label={t.label} note={bind.description}>
                  <Switch label={binding ? 'Sent' : 'Not sent'} checked={binding !== undefined} disabled={last}
                    onChange={(on) => setStream(at, (o) => ({ ...o, bindings: on ? [...o.bindings, { provider: t.value, endpoint: t.value === bleTransport ? (charChoices[0]?.value ?? 0) : 0 }] : o.bindings.filter((b) => b.provider !== t.value) }))} />
                </FormRow>
                {binding && t.value === bleTransport && (
                  <FormRow label={bind.write?.args?.fields.find((f) => f.id === 'endpoint')?.label ?? 'Endpoint'} note={bind.write?.args?.fields.find((f) => f.id === 'endpoint')?.description}>
                    <ChoiceField<number> aria-label={`${title(s)}: characteristic`} value={binding.endpoint} choices={charChoices} unavailableLabel={(u) => hex(u)}
                      onChange={(endpoint) => endpoint !== undefined && setStream(at, (o) => ({ ...o, bindings: o.bindings.map((b) => (b.provider === t.value ? { ...b, endpoint } : b)) }))} />
                  </FormRow>
                )}
              </FormGrid>
            )
          })}
        </Card>
      ))}
      <div className="project-actions">
        <Button onClick={() => {
          let stream = 0x10
          while (streams.some((s) => s.stream === stream) || streamNames.has(stream)) stream++
          set([...streams, { stream, system: false, suspended: false, bindings: [] }])
        }}><Plus aria-hidden="true" />{catalog.command(entry, STREAMS.create).label}</Button>
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
      {section.record?.editor === 'gatt' && <GattEditor section={section} state={state} context={context} />}
      {section.record?.editor === 'streams' && <StreamsEditor section={section} state={state} context={context} />}
    </>
  )
}

/** A settings tab: the section's title, Read, and its cards. */
export function SettingsScreen({ section, state, context, nameOf }: { section: SettingSection; state: BoardSettingsState; context: PinContext; nameOf: (device: number) => string }) {
  return (
    <div className="project-screen">
      <PanelHeader title={section.title}>
        {state.connected && <Button size="sm" title="Read these settings from the board: the forms show what it has (Undo brings the project's back)" onClick={() => state.read(section)}><RefreshCw aria-hidden="true" />Read</Button>}
      </PanelHeader>
      {!state.connected && <p className="project-note">Not connected: the values are the project's; connect the board to see its own.</p>}
      <CardStack>
        {section.service.key !== undefined && <ErrorsCard about={`router_key:${section.service.key}`} />}
        <SettingsCards section={section} state={state} context={context} nameOf={nameOf} />
      </CardStack>
    </div>
  )
}
