import { useState } from 'react'
import { Cable, Download, FlipHorizontal2, Funnel, Gauge, HardDriveUpload, Link2, RotateCw, SlidersHorizontal, Trash2, Zap } from 'lucide-react'
import type { RunitBleSession } from '../backend/runitBleSession'
import { ToggleChip } from '../components/Button'
import { runitErrorCatalog } from '../domain/descriptors'
import { deviceInstallSteps, deviceSetupSteps } from '../domain/devices'
import type { ProjectSettings } from '../domain/project'
import type { UploadDiagnostic, UploadStep } from '../domain/upload'
import { storeCode } from '../boardCode'
import { DEFAULT_MAX_FRAME_BYTES } from '../frameLimits'
import { sendSteps } from '../sendSteps'
import type { BoardCodeState } from '../useBoardCode'
import { useHardwareConfig } from '../useProjectCode'
import { useBoardModel } from './board/boardModel'
import { mirrored, placementOf, rotated } from './board/symbols'
import type { BoardWiring } from './useBoardWiring'
import type { DevicesWorkspace } from './useDevicesWorkspace'

/** Install all devices, apply all default settings, save the hardware config to flash: shared by the canvas and the manage toolbar. */
export function BoardSendTools({ workspace: w, session, settings, board }: { workspace: DevicesWorkspace; session?: RunitBleSession; settings: ProjectSettings; board: BoardCodeState }) {
  const [busy, setBusy] = useState(false)
  const [result, setResult] = useState<{ ok: boolean; text: string }>()
  const send = async (label: string, plan: { steps: readonly UploadStep[]; diagnostics: readonly UploadDiagnostic[] }) => {
    const problem = plan.diagnostics.find((entry) => entry.severity === 'error')
    if (problem) return setResult({ ok: false, text: `${label}: ${problem.message}` })
    if (!session) return setResult({ ok: false, text: `${label}: connect a board first.` })
    setBusy(true)
    try {
      await sendSteps(session, plan.steps)
      setResult({ ok: true, text: `${label}: ${plan.steps.length} command${plan.steps.length === 1 ? '' : 's'} sent` })
    } catch (error) {
      setResult({ ok: false, text: `${label}: ${error instanceof Error ? error.message : String(error)}` })
    } finally {
      setBusy(false)
    }
  }
  const { config, slot } = useHardwareConfig({ settings, devices: w.devices, deviceCatalog: w.catalog, setup: w.setup })
  const saveConfig = async () => {
    const problem = config.diagnostics.find((entry) => entry.severity === 'error')
    if (problem) return setResult({ ok: false, text: `Save hardware config: ${problem.message}` })
    if (!session) return setResult({ ok: false, text: 'Save hardware config: connect a board first.' })
    setBusy(true)
    try {
      await storeCode(session, 'config', slot.bytes, runitErrorCatalog().schemaId, DEFAULT_MAX_FRAME_BYTES)
      board.refresh()
      setResult({ ok: true, text: `Hardware config saved: ${slot.frames} frames. The board replays it at every boot.` })
    } catch (error) {
      setResult({ ok: false, text: `Save hardware config: ${error instanceof Error ? error.message : String(error)}` })
    } finally {
      setBusy(false)
    }
  }
  const installAll = () => send('Install devices', deviceInstallSteps(w.catalog, w.devices))
  const applyDefaults = () => send('Default settings', deviceSetupSteps(w.catalog, w.devices, w.setup))
  return (
    <div className="history-actions board-tools" role="group" aria-label="Send to the board">
    <button type="button" aria-label="Install all devices" title={session ? 'Install every device of the project on the board now (devices already installed are refused). Stored code installs them at every boot.' : 'Connect a board to install the devices'} disabled={busy || !w.devices.length} onClick={() => void installAll()}><Download aria-hidden="true" /></button>
    <button type="button" aria-label="Apply all default settings" title={session ? "Send every device's default settings to the board now. Stored code sets them at every boot." : 'Connect a board to apply the default settings'} disabled={busy || !w.setup.length} onClick={() => void applyDefaults()}><SlidersHorizontal aria-hidden="true" /></button>
    <button type="button" aria-label="Save hardware config to the board" title={session ? 'Store the hardware config in the flash of the board: BLE, connectors, every device and its default settings. The program is not touched.' : 'Connect a board to save the hardware config'} disabled={busy || !config.ok} onClick={() => void saveConfig()}><HardDriveUpload aria-hidden="true" /></button>
    {result && <span className={`board-send-result ${result.ok ? '' : 'is-error'}`} role="status" title={result.text}>{result.text}</span>}
  </div>
  )
}

/*
 * The Board canvas's tools in the app's top toolbar: rotate a quarter turn and mirror the selected device (the R and M
 * keys do the same on the canvas), delete the selected wire, and the wiring view options (board-canvas-concept.md §6).
 * They live here because a press on the canvas surface itself deselects.
 */
export function BoardToolbar({ workspace: w, wiring, session, settings, board }: { workspace: DevicesWorkspace; wiring: BoardWiring; session?: RunitBleSession; settings: ProjectSettings; board: BoardCodeState }) {
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
      <BoardSendTools workspace={w} session={session} settings={settings} board={board} />
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
