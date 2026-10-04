import { Cable, FlipHorizontal2, Funnel, Gauge, Link2, RotateCw, Trash2, Zap } from 'lucide-react'
import { ToggleChip } from '../components/Button'
import { useBoardModel } from './board/boardModel'
import { mirrored, placementOf, rotated } from './board/symbols'
import type { BoardWiring } from './useBoardWiring'
import type { DevicesWorkspace } from './useDevicesWorkspace'

/*
 * The Board canvas's tools in the app's top toolbar: rotate a quarter turn and mirror the selected device (the R and M
 * keys do the same on the canvas), delete the selected wire, and the wiring view options (board-canvas-concept.md §6).
 * They live here because a press on the canvas surface itself deselects.
 */
export function BoardToolbar({ workspace: w, wiring }: { workspace: DevicesWorkspace; wiring: BoardWiring }) {
  const ref = w.selection?.kind === 'device' ? w.selection.ref : undefined
  const model = useBoardModel(w.board, w.catalog)
  const current = ref ? placementOf(w.devices, w.catalog, w.boardLayout, ref, model.widthMm) : undefined
  const apply = (change: typeof rotated) => {
    if (ref && current) w.setPlacement(ref, change(current))
  }
  const wire = wiring.selectedWire
  return (
    <>
      <div className="history-actions board-tools" role="group" aria-label="Device placement">
        <button type="button" aria-label="Rotate (R)" title={current ? 'Rotate 90° (R)' : 'Select a device on the canvas to rotate it'} disabled={!current} onClick={() => apply(rotated)}><RotateCw aria-hidden="true" /></button>
        <button type="button" aria-label="Mirror (M)" title={current ? 'Mirror (M)' : 'Select a device on the canvas to mirror it'} disabled={!current} onClick={() => apply(mirrored)}><FlipHorizontal2 aria-hidden="true" /></button>
        <button type="button" aria-label="Delete wire" title={wire ? 'Delete the selected wire (Delete)' : 'Click a wire to select it'} disabled={!wire} onClick={() => { if (wire) { w.removeWire(wire); wiring.selectWire(undefined) } }}><Trash2 aria-hidden="true" /></button>
      </div>
      <div className="board-wiring-options" role="group" aria-label="Wiring view">
        <ToggleChip selected={wiring.power} title="Power networks: supply and ground wires (off: faded)" onClick={() => wiring.toggle('power')}><Zap aria-hidden="true" size={13} /> Power</ToggleChip>
        <ToggleChip selected={wiring.io} title="IO networks: signal and bus wires (off: faded)" onClick={() => wiring.toggle('io')}><Cable aria-hidden="true" size={13} /> IO</ToggleChip>
        <ToggleChip selected={wiring.focus} title="Device connections: fade the wires that are not on the selected device" onClick={() => wiring.toggle('focus')}><Link2 aria-hidden="true" size={13} /> Device</ToggleChip>
        <ToggleChip selected={wiring.voltages} title="Voltages: label each wire with its net's voltage" onClick={() => wiring.toggle('voltages')}><Gauge aria-hidden="true" size={13} /> Volts</ToggleChip>
        <ToggleChip selected={wiring.availableOnly} title="Available only: while dragging a wire, ring only the pins that can take it" onClick={() => wiring.toggle('availableOnly')}><Funnel aria-hidden="true" size={13} /> Available</ToggleChip>
      </div>
    </>
  )
}
