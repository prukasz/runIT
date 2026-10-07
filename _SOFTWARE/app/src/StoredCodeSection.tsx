import { useState } from 'react'
import { Button } from './components/Button'
import { TextField } from './components/FormField'
import { AlertCircle, AlertTriangle, CheckCircle2, HardDriveUpload, RotateCcw, Trash2 } from 'lucide-react'
import { eraseCode, loadCode, REPLAY_STATES, SLOT_NAMES, STORED_SLOT_KEYS, setCodeAutostart, storeCode } from './boardCode'
import type { StoredSlot } from './boardCode'
import { errorOwnerName, errorTagName } from './domain/decoder'
import { runitErrorCatalog } from './domain/descriptors'
import type { DeviceCatalog } from './domain/descriptors'
import type { ActionStep, ObjectSection, ProjectDevice, ProjectDocument, ProjectSettings } from './domain/project'
import type { BoardCodeState } from './useBoardCode'
import { useProjectCode } from './useProjectCode'
import type { RunitBleSession } from './backend/runitBleSession'

/*
 * "Code is ready": the project built into the two lists a board replays at boot
 * (app/docs/03_records_as_storage.md): the hardware config (BLE, connectors,
 * devices and their default settings), replayed first, and the code (the VM
 * program). Each is stored and erased on its own; Load restarts the board with
 * both. Recovery and code files are in the File view.
 */

const errors = runitErrorCatalog()
const hex8 = (value: number): string => `0x${value.toString(16).padStart(8, '0').toUpperCase()}`
const kb = (bytes: number): string => (bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`)

const SLOT_HINT: Readonly<Record<StoredSlot, string>> = {
  config: 'BLE services, connectors, installed devices and their default settings.',
  program: 'The blocks and objects of the canvases.',
}

interface Props {
  readonly project: ProjectDocument
  readonly sections: readonly ObjectSection[]
  readonly settings: ProjectSettings
  readonly devices: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
  readonly setup: readonly ActionStep[]
  readonly board: BoardCodeState
  readonly session?: RunitBleSession
  /** Longest command the link takes (store chunks). */
  readonly maxFrameBytes: number
  readonly setAutostart: (autostart: boolean) => void
  readonly note: (ok: boolean, text: string) => void
}

export function StoredCodeSection({ project, sections, settings, devices, deviceCatalog, setup, board, session, maxFrameBytes, setAutostart, note }: Props) {
  const [busy, setBusy] = useState<string>()
  const [progress, setProgress] = useState<{ done: number; total: number }>()
  const { code, slots } = useProjectCode({ project, sections, settings, devices, deviceCatalog, setup })
  const autostart = project.autostart ?? false
  const { infos } = board
  const schemaMismatch = infos && infos.config.firmwareSchemaId !== errors.schemaId
  const ok: Readonly<Record<StoredSlot, boolean>> = { config: code.configOk, program: code.programOk }
  const matches = (slot: StoredSlot): boolean => !!infos && infos[slot].stored && infos[slot].crc32 === slots[slot].crc && infos[slot].schemaId === errors.schemaId
  const problems = (slot: StoredSlot) => (slot === 'config' ? code.configDiagnostics : code.diagnostics.filter((entry) => !code.configDiagnostics.includes(entry))).filter((entry) => entry.severity === 'error')

  const run = async (label: string, operation: () => Promise<string>) => {
    setBusy(label)
    try {
      note(true, await operation())
    } catch (error) {
      note(false, `${label}: ${error instanceof Error ? error.message : String(error)}`)
    } finally {
      setBusy(undefined)
      setProgress(undefined)
    }
  }

  const store = (slot: StoredSlot) => run(`Store ${SLOT_NAMES[slot].toLowerCase()}`, async () => {
    if (!session) throw new Error('Connect a runIT board first.')
    const { bytes, crc, frames } = slots[slot]
    const stored = await storeCode(session, slot, bytes, errors.schemaId, maxFrameBytes, (done, total) => setProgress({ done, total }))
    if (slot === 'program' && stored.autostart !== autostart) await setCodeAutostart(session, autostart)
    board.refresh()
    return `Stored ${SLOT_NAMES[slot].toLowerCase()}: ${frames} frames (${kb(bytes.byteLength)}, CRC ${hex8(crc)})${slot === 'program' ? `, autostart ${autostart ? 'on' : 'off'}` : ''}. Load restarts the board with it.`
  })

  const load = () => {
    if (!window.confirm('Restart the board? It replays its stored hardware config and code at boot; the connection drops and you reconnect afterwards.')) return
    void run('Load', async () => {
      if (!session) throw new Error('Connect a runIT board first.')
      await loadCode(session)
      return 'The board restarts with its stored code: reconnect, then check the replay report here.'
    })
  }

  const erase = (slot: StoredSlot) => {
    if (!window.confirm(`Erase the stored ${SLOT_NAMES[slot].toLowerCase()}? The board keeps running until it restarts, then starts that part from its defaults.`)) return
    void run(`Erase ${SLOT_NAMES[slot].toLowerCase()}`, async () => {
      if (!session) throw new Error('Connect a runIT board first.')
      await eraseCode(session, slot)
      board.refresh()
      return `Stored ${SLOT_NAMES[slot].toLowerCase()} erased.`
    })
  }

  const replay = infos?.config.replay
  const disabled = !session || !!busy

  return (
    <div className="object-details-section">
      <h3>Stored code</h3>
      <p className="program-muted">What the board replays at every boot, hardware config first. Live uploads and Apply change the running board only; store to keep them.</p>
      {session && infos && (
        <dl className="program-facts">
          <div><dt>Board autostart</dt><dd>{infos.program.autostart ? 'on' : 'off'}</dd></div>
          {replay && <div><dt>Last boot</dt><dd>{REPLAY_STATES[replay.state] ?? `state ${replay.state}`}{replay.state === 1 ? ` · ${replay.applied} applied${replay.failed ? `, ${replay.failed} refused` : ''}` : ''}</dd></div>}
        </dl>
      )}
      {session && board.reading && <p className="program-muted">Reading the board…</p>}
      {schemaMismatch && <p className="program-diag is-error"><AlertCircle aria-hidden="true" />The board's firmware (schema {hex8(infos.config.firmwareSchemaId)}) differs from the app's descriptors ({hex8(errors.schemaId)}): it refuses this code.</p>}
      {infos && replay && replay.firstFailed !== undefined && (
        <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />Boot replay refused {SLOT_NAMES[replay.firstSlot].toLowerCase()} frame {replay.firstFailed} first: {errorTagName(errors, replay.firstTag)} ({errorOwnerName(errors, replay.firstOwner)}).</p>
      )}
      {board.note && <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />{board.note}</p>}

      {STORED_SLOT_KEYS.map((slot) => {
        const info = infos?.[slot]
        return (
          <div key={slot} className="stored-slot">
            <h4>{SLOT_NAMES[slot]}</h4>
            <p className="program-muted">{SLOT_HINT[slot]}</p>
            <dl className="program-facts">
              <div><dt>Project</dt><dd>{ok[slot] ? `${slots[slot].frames} frames · ${kb(slots[slot].bytes.byteLength)} · ${hex8(slots[slot].crc)}` : 'has errors'}</dd></div>
              {session && info && <div><dt>On the board</dt><dd>{info.stored ? `${info.frameCount} frames · ${kb(info.length)} / ${kb(info.capacity)} · ${hex8(info.crc32)}` : 'none'}</dd></div>}
            </dl>
            {problems(slot).map((entry, index) => <p key={`e${index}`} className="program-diag is-error"><AlertCircle aria-hidden="true" />{entry.message}</p>)}
            {info && ok[slot] && !schemaMismatch && (matches(slot)
              ? <p className="program-diag"><CheckCircle2 aria-hidden="true" />The board stores this project's {SLOT_NAMES[slot].toLowerCase()}.</p>
              : <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />{info.stored ? `The ${SLOT_NAMES[slot].toLowerCase()} on the board differs from this project.` : `No ${SLOT_NAMES[slot].toLowerCase()} stored on the board.`}</p>)}
            {slot === 'program' && infos && infos.program.stored && !matches('config') && (
              <p className="program-muted">The hardware config on the board differs: the stored code may refer to devices it lacks.</p>
            )}
            <div className="program-actions">
              <Button variant="primary" disabled={disabled || !ok[slot] || !!schemaMismatch} onClick={() => void store(slot)} title={`Write the project's ${SLOT_NAMES[slot].toLowerCase()} to the board (replaces what it stored)`}>
                <HardDriveUpload aria-hidden="true" /><span>{busy === `Store ${SLOT_NAMES[slot].toLowerCase()}` && progress ? `${progress.done}/${progress.total}` : 'Store'}</span>
              </Button>
              <Button disabled={disabled || !info?.stored} onClick={() => erase(slot)} title={`Erase the stored ${SLOT_NAMES[slot].toLowerCase()}`}><Trash2 aria-hidden="true" /><span>Erase</span></Button>
            </div>
          </div>
        )
      })}

      <label className="program-check">
        <TextField type="checkbox" checked={autostart} onChange={(event) => setAutostart(event.target.checked)} />
        <span>Run the program after boot (autostart; saved with Store code)</span>
      </label>
      <div className="program-actions">
        <Button disabled={disabled} onClick={load} title="Restart the board: it replays its stored hardware config and code"><RotateCcw aria-hidden="true" /><span>Load</span></Button>
      </div>
    </div>
  )
}
