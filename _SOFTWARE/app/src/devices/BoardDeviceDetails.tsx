import { Badge } from '../components/Badge'
import { NumberField } from '../components/FormField'
import type { PinLimit } from '../domain/project'
import { PanelHeader } from '../components/PanelHeader/PanelHeader'
import { resolveDevice } from '../domain/devices'
import { DeviceTile } from './DeviceTile'
import { placements, sizeMm, symbolOf, variantOf } from './board/symbols'
import { variantsOf } from './board/pictureSymbols'
import { useWiringReport } from './board/wiringReport'
import { DeviceWiring, WireCard, WiringProblems } from './BoardWiringDetails'
import type { BoardWiring } from './useBoardWiring'
import { useBoardModel } from './board/boardModel'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import './Devices.css'

/*
 * The right bar of the Board canvas: the selected device's variants, the ways it can be drawn and used on the board
 * (the chip itself, breakout boards …). A variant's hidden pins are ones it does not bring out; they show greyed
 * inside the symbol and cannot be wired.
 */
export function BoardDeviceDetails({ workspace: w, wiring }: { workspace: DevicesWorkspace; wiring: BoardWiring }) {
  const ref = w.selection?.kind === 'device' ? w.selection.ref : undefined
  const model = useBoardModel(w.board, w.catalog)
  const report = useWiringReport(w, model)
  const wire = wiring.selectedWire ? w.wires.find((entry) => entry.id === wiring.selectedWire) : undefined
  const device = ref ? resolveDevice(w.catalog, w.devices, ref) : undefined
  const project = w.devices.find((entry) => entry.id === ref)

  if (!device || !project) {
    return (
      <div className="program-panel devices-details">
        <PanelHeader title={wire ? 'Wire' : 'Board'} />
        {wire && <WireCard workspace={w} report={report} wiring={wiring} wire={wire} />}
        {!wire && <p className="devices-muted">Select a device on the canvas to choose how it is drawn (the chip, or a breakout board) and to see its wiring. Press a board terminal or a device pin and drag to another to wire them.</p>}
        <WiringProblems workspace={w} report={report} wiring={wiring} />
      </div>
    )
  }

  const variants = variantsOf(device.type)
  const current = variantOf(device.type, w.boardLayout?.[project.id]?.variant)
  const choose = (id: string) => {
    // An unplaced device is placed where the canvas shows it, so the choice has a position to live in.
    const placed = placements(w.devices, w.boardLayout, model.widthMm, (entry) => sizeMm(symbolOf(w.catalog.type(entry.type), w.boardLayout?.[entry.id]?.variant))).get(project.id)
    if (placed) w.setPlacement(project.id, { ...placed, variant: id })
  }
  const pinLimits = w.boardLayout?.[project.id]?.pinLimits ?? {}
  // Voltage the user allows on a pin above the chip's own limit (a divider or clamp in front of it); empty = the pins csv's value.
  const setMax = (pin: string, volts: number | undefined) => {
    const placed = placements(w.devices, w.boardLayout, model.widthMm, (entry) => sizeMm(symbolOf(w.catalog.type(entry.type), w.boardLayout?.[entry.id]?.variant))).get(project.id)
    if (!placed) return
    const next: Record<string, PinLimit> = { ...pinLimits }
    if (volts === undefined) delete next[pin]
    else next[pin] = { ...next[pin], mV_max: Math.round(volts * 1000) }
    const { pinLimits: _old, ...rest } = placed
    w.setPlacement(project.id, { ...rest, ...(Object.keys(next).length ? { pinLimits: next } : {}) })
  }
  const limited = device.type.ports.filter((port) => port.kind === 'io' && port.mV_max !== undefined)

  return (
    <div className="program-panel devices-details">
      <PanelHeader icon={<DeviceTile appearance={project.appearance} type={device.type} size="small" />} title={device.name}>
        <Badge push>#{device.deviceId}</Badge>
      </PanelHeader>
      {wire && <WireCard workspace={w} report={report} wiring={wiring} wire={wire} />}
      <DeviceWiring workspace={w} report={report} wiring={wiring} deviceRef={project.id} />
      <div className="board-variants">
        <h3>Variants</h3>
        {variants.length ? (
          <ul role="radiogroup" aria-label="Variants">
            {variants.map((variant) => (
              <li key={variant.id}>
                <button type="button" role="radio" aria-checked={variant.id === current?.id} className={variant.id === current?.id ? 'is-selected' : ''} onClick={() => choose(variant.id)}>
                  <span className="board-variant-text">
                    <strong>{variant.label}</strong>
                    <small>{variant.picture ? 'Picture' : variant.package || 'Generic symbol'}{variant.hidden.length ? ` · not brought out: ${variant.hidden.join(', ')}` : ' · every pin brought out'}</small>
                  </span>
                </button>
              </li>
            ))}
          </ul>
        ) : (
          <p className="devices-muted">This device has no symbol yet; it is drawn as a plain box.</p>
        )}
      </div>
      {limited.length > 0 && (
        <div className="board-variants">
          <h4>Pin voltage limits</h4>
          <p className="devices-muted">The chip's own maximum is shown. Raise it for a pin that has a voltage divider or clamp in front of it (volts).</p>
          <ul>
            {limited.map((port) => (
              <li key={port.name} className="board-pin-limit">
                <span>{port.name}</span>
                <NumberField aria-label={`${port.name} maximum voltage`} min={0} max={100} step={0.1} value={(pinLimits[port.name]?.mV_max ?? port.mV_max ?? 0) / 1000} onCommit={(volts) => setMax(port.name, volts * 1000 === port.mV_max ? undefined : volts)} />
                {pinLimits[port.name]?.mV_max !== undefined && <button type="button" onClick={() => setMax(port.name, undefined)}>Reset</button>}
              </li>
            ))}
          </ul>
        </div>
      )}
      <WiringProblems workspace={w} report={report} wiring={wiring} />
    </div>
  )
}
