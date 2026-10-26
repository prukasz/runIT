import { Badge, Button, Card, CardStack, ChoiceField, FormRow, NumberField, PanelHeader } from '@ui/components'
import { Lock, RotateCcw, RotateCw, Trash2 } from 'lucide-react'
import { elementInputs, svgUrl } from './library'
import type { LibraryPart } from './library'
import { BOARD_ITEM, endKey } from './layout'
import { groupOf } from './netlist'
import type { PinNode } from './netlist'
import { amps, pathTo, rangeText, volts } from './power'
import type { ElementState, PowerProblem } from './power'
import type { HardwareState } from './useHardware'

/*
 * The right panel of the Devices canvas: what is selected. A part: its device (which the Devices view opens), what it wants of
 * its supply and what it gets, the way the energy comes to it from the source. A pin: what it is, what it is wired to, the way
 * to it. The board: the power choices (the source and its voltage, each mux's input, each regulator's voltage: picked by the
 * canvas for what is connected and locked to it, or set by hand) and everything the canvas finds wrong.
 */

const PIN_KIND: Readonly<Record<PinNode['kind'], string>> = {
  gnd: 'Ground', power: 'Power', provider: 'A pin of a device', consumer: 'Takes a pin of a device', bus: 'Bus line', hbridge: 'H-bridge output', motor: 'Motor lead', nc: 'Not connected', none: 'Not set',
}

function Problems({ problems }: { problems: readonly PowerProblem[] }) {
  if (problems.length === 0) return <p className="muted">Nothing wrong with the power.</p>
  return <ul className="hw-problems">{problems.map((p, i) => <li key={i} className={p.severity === 'error' ? 'is-error' : ''}>{p.text}</li>)}</ul>
}

/** The way energy comes to a power node: from it up to the source, with voltage and current at each step. */
function PowerPath({ state, node }: { state: HardwareState; node: string }) {
  const steps = pathTo(state.power, node)
  if (steps.length === 0) return null
  return (
    <ol className="hw-path">
      {steps.map(({ node: n, element }) => (
        <li key={n.id}>
          <span className="hw-path-name">{n.label}</span>
          <span className="hw-path-value">{volts(n.voltage)} · {amps(n.current.typical)} (peak {amps(n.current.peak)})</span>
          {element && <span className="hw-path-sub">← {element.element.label}{element.lockedBy && !element.byUser ? ' · set by the canvas' : element.byUser ? ' · set by hand' : ''}</span>}
        </li>
      ))}
    </ol>
  )
}

function PartCard({ state, part, item, onOpenDevice }: { state: HardwareState; part: LibraryPart; item: string; onOpenDevice: (id: number) => void }) {
  const placed = state.net.parts.find((p) => p.id === item)
  const device = placed?.device === undefined ? undefined : state.ctx.devices.find((d) => d.id === placed.device)
  const sinks = (part.nets ?? []).filter((n) => n.role === 'sink')
  return (
    <>
      <PanelHeader title={device?.name ?? part.name}>{part.origin === 'project' && <Badge tone="info">this project</Badge>}</PanelHeader>
      <CardStack>
        <Card title={part.name} actions={(
          <>
            <button aria-label="Turn left" title="Turn left (Shift+R or Shift+Space)" onClick={() => state.rotate(item, -90)}><RotateCcw aria-hidden="true" /></button>
            <button aria-label="Turn right" title="Turn right (R or Space)" onClick={() => state.rotate(item, 90)}><RotateCw aria-hidden="true" /></button>
            {item !== BOARD_ITEM && <button className="toolbar-danger" aria-label="Remove" title="Remove the part, its wires and its device" onClick={() => state.remove({ kind: 'item', id: item })}><Trash2 aria-hidden="true" /></button>}
          </>
        )}>
          <img className="hw-preview" src={svgUrl(part.svg)} alt="" />
          {part.description && <p>{part.description}</p>}
          <dl className="hw-kv">
            <dt>Size</dt><dd>{part.size_mm.w} × {part.size_mm.h} mm</dd>
            {device && <><dt>Device</dt><dd>{device.name} · {device.type?.label ?? part.device?.type}</dd></>}
          </dl>
          {device && <Button onClick={() => onOpenDevice(device.id)}>Open its settings</Button>}
        </Card>
        {sinks.map((net) => {
          const node = state.power.nodeOf.get(`${item}:${net.id}`)
          const now = node ? state.power.nodes.get(node) : undefined
          return (
            <Card key={net.id} title={`Power: ${net.label}`}>
              <dl className="hw-kv">
                <dt>Wants</dt><dd>{rangeText(net.voltage_mV)}{net.voltage_mV?.nominal ? ` (best ${volts(net.voltage_mV.nominal)})` : ''}</dd>
                <dt>Draws</dt><dd>{amps(net.current_mA?.typical ?? 0)} typical, {amps(net.current_mA?.peak ?? net.current_mA?.typical ?? 0)} peak</dd>
                <dt>Gets</dt><dd>{now?.voltage !== undefined ? volts(now.voltage) : 'nothing yet'}</dd>
              </dl>
              {node && <PowerPath state={state} node={node} />}
            </Card>
          )
        })}
        {state.power.problems.some((p) => p.item === item) && <Card title="What is wrong"><Problems problems={state.power.problems.filter((p) => p.item === item)} /></Card>}
      </CardStack>
    </>
  )
}

function PinCard({ state, node }: { state: HardwareState; node: PinNode }) {
  const joined = groupOf(state.net, node.key).filter((m) => m.end.item !== node.end.item)
  const wires = state.layout.wires.filter((w) => endKey(w.a) === node.key || endKey(w.b) === node.key)
  const powerNode = node.pin.net !== undefined ? state.power.nodeOf.get(`${node.end.item}:${node.pin.net}`) : undefined
  const device = node.device === undefined ? undefined : state.ctx.devices.find((d) => d.id === node.device)
  return (
    <>
      <PanelHeader title={`${node.pin.label} · ${node.connector.label ?? node.connector.id}`} />
      <CardStack>
        <Card title={node.part.name}>
          <dl className="hw-kv">
            <dt>What</dt><dd>{PIN_KIND[node.kind]}{node.pin.description ? ` · ${node.pin.description}` : ''}</dd>
            {node.kind === 'provider' && device && <><dt>Device</dt><dd>{device.name}, pin #{node.pin.device_pin!.pin}</dd></>}
            {node.kind === 'consumer' && <><dt>Field</dt><dd>{device?.type?.config?.fields.find((f) => f.id === node.pin.config)?.label ?? node.pin.config}</dd></>}
            {node.pin.bus && <><dt>Bus</dt><dd>{node.pin.bus.type.toUpperCase()} {node.pin.bus.line.toUpperCase()}{node.pin.bus.bus !== undefined ? ` (bus ${node.pin.bus.bus})` : ''}</dd></>}
            <dt>Wired to</dt><dd>{joined.length > 0 ? joined.map((m) => `${m.pin.label} (${m.part.name})`).join(', ') : 'nothing'}</dd>
          </dl>
          <p className="muted">{state.picked?.key === node.key ? 'Tap a green pin to wire it there; Esc lets it go.' : 'Tap the pin again on the canvas to pick it: the pins it may go to light up.'}</p>
          {wires.length > 0 && <Button variant="danger" onClick={() => state.remove({ kind: 'pin', key: node.key })}>Remove its wires</Button>}
        </Card>
        {powerNode && <Card title="Where its power comes from"><PowerPath state={state} node={powerNode} /></Card>}
      </CardStack>
    </>
  )
}

/** A regulator or a mux: the canvas's choice, locked to what asked for it, or the user's. */
function ElementRow({ state, id, e }: { state: HardwareState; id: string; e: ElementState }) {
  const { element } = e
  const why = e.lockedBy?.length ? `For ${[...new Set(e.lockedBy.map((s) => state.net.parts.find((p) => p.id === s.item)?.part.name ?? s.item))].join(', ')}` : undefined
  if (element.kind === 'mux') {
    const choices = [{ value: -1, label: 'Auto (for what it feeds)' }, ...elementInputs(element).map((_, i) => ({ value: i, label: element.inputs?.[i] ?? `Input ${i + 1}` }))]
    return (
      <FormRow label={element.label} hint={<>{e.input !== undefined ? `Now: ${element.inputs?.[e.input] ?? e.input}` : 'Off: nothing on it'}{why && !e.byUser ? <> · <Lock size={11} /> {why}</> : null}</>}>
        <ChoiceField value={e.byUser ? e.input : -1} choices={choices} onChange={(v) => state.setPower((p) => {
          const muxes = { ...p.muxes }
          if (v === undefined || v < 0) delete muxes[element.id]
          else muxes[element.id] = v
          return { ...p, muxes }
        })} />
      </FormRow>
    )
  }
  const range = element.voltage_mV
  return (
    <FormRow label={element.label} hint={<>{e.setpoint_mV !== undefined ? `${volts(e.setpoint_mV)}` : 'Off: nothing on it'}{e.active ? ` · ${amps(e.current.typical)} (peak ${amps(e.current.peak)})` : ''}{why && !e.byUser ? <> · <Lock size={11} /> {why}</> : null}</>}>
      <NumberField value={e.byUser ? e.setpoint_mV : undefined} placeholder={`Auto${e.setpoint_mV !== undefined ? `: ${e.setpoint_mV}` : ''} mV`} min={range?.min} max={range?.max} integer
        onCommit={(v) => state.setPower((p) => ({ ...p, rails: { ...p.rails, [element.id]: v } }))} />
      {e.byUser && <Button size="sm" onClick={() => state.setPower((p) => { const rails = { ...p.rails }; delete rails[element.id]; return { ...p, rails } })}>Auto</Button>}
    </FormRow>
  )
}

function BoardCard({ state, part }: { state: HardwareState; part: LibraryPart }) {
  const elements = [...state.power.elements].filter(([, e]) => e.item === BOARD_ITEM)
  const sources = elements.filter(([, e]) => e.element.kind === 'source')
  const source = state.power.source ? state.power.elements.get(state.power.source.element) : undefined
  const choices = state.layout.power
  return (
    <>
      <PanelHeader title={part.name} />
      <CardStack>
        <Card title="Power in" help="Where the board's energy comes from: the canvas picks what is not set here for what is connected.">
          <FormRow label="Source">
            <ChoiceField value={source?.element.id} choices={sources.map(([, e]) => ({ value: e.element.id, label: e.element.label, ...(e.element.description ? { description: e.element.description } : {}) }))}
              onChange={(v) => state.setPower((p) => { const { input_mV: _drop, ...rest } = p; return { ...rest, ...(v ? { source: v } : {}) } })} />
          </FormRow>
          {source?.element.levels_mV ? (
            <FormRow label="USB PD voltage" hint={`Now: ${volts(state.power.source?.mV)}`}>
              <ChoiceField value={choices.input_mV ?? -1} choices={[{ value: -1, label: 'Auto (the lowest that fits and carries the load)' }, ...source.element.levels_mV.map((l) => ({ value: l, label: volts(l) }))]}
                onChange={(v) => state.setPower((p) => { const { input_mV: _drop, ...rest } = p; return v === undefined || v < 0 ? rest : { ...rest, input_mV: v } })} />
            </FormRow>
          ) : source && (
            <FormRow label="Its voltage (mV)" hint={`${rangeText(source.element.voltage_mV)}: what the battery or the PSU gives`}>
              <NumberField value={choices.input_mV} min={source.element.voltage_mV?.min} max={source.element.voltage_mV?.max} integer placeholder="Not set" onCommit={(v) => state.setPower((p) => ({ ...p, input_mV: v }))} />
            </FormRow>
          )}
        </Card>
        <Card title="Rails and supplies" help="A regulator the canvas sets is locked to what is on it (its start commands in the Devices view say so); a number set here wins.">
          {elements.filter(([, e]) => (e.element.kind === 'regulator' && e.element.adjustable) || e.element.kind === 'mux').map(([id, e]) => <ElementRow key={id} state={state} id={id} e={e} />)}
        </Card>
        <Card title="Load">
          <dl className="hw-kv">
            {elements.filter(([, e]) => e.active && e.element.kind !== 'load').map(([id, e]) => (
              <span key={id} style={{ display: 'contents' }}><dt>{e.element.label}</dt><dd>{volts(e.out_mV ?? e.in_mV)} · {amps(e.current.typical)} (peak {amps(e.current.peak)}){e.element.max_mA ? ` of ${amps(e.element.max_mA)}` : ''}</dd></span>
            ))}
          </dl>
        </Card>
        <Card title="What is wrong"><Problems problems={state.power.problems} /></Card>
      </CardStack>
    </>
  )
}

export function HardwareDetails({ state, onOpenDevice }: { state: HardwareState; onOpenDevice: (id: number) => void }) {
  const sel = state.selection
  if (sel?.kind === 'item') {
    const placed = state.net.parts.find((p) => p.id === sel.id)
    if (!placed) return null
    return sel.id === BOARD_ITEM ? <BoardCard state={state} part={placed.part} /> : <PartCard state={state} part={placed.part} item={sel.id} onOpenDevice={onOpenDevice} />
  }
  if (sel?.kind === 'pin') {
    const node = state.net.nodes.get(sel.key)
    return node ? <PinCard state={state} node={node} /> : null
  }
  if (sel?.kind === 'wire') {
    const w = state.layout.wires.find((x) => x.id === sel.id)
    const [a, b] = w ? [state.net.nodes.get(endKey(w.a)), state.net.nodes.get(endKey(w.b))] : []
    return (
      <>
        <PanelHeader title="Wire" />
        <Card title={a && b ? `${a.pin.label} — ${b.pin.label}` : 'Wire'}>
          {a && b && <p>{a.part.name} · {a.connector.label ?? a.connector.id} → {b.part.name} · {b.connector.label ?? b.connector.id}</p>}
          <Button variant="danger" onClick={() => state.remove(sel)}>Remove the wire</Button>
        </Card>
      </>
    )
  }
  return null
}
