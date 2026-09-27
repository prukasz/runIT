import { useMemo, useState } from 'react'
import { AlertCircle, AlertTriangle, CheckCircle2, Download, HardDriveDownload, HardDriveUpload, RotateCcw, Trash2 } from 'lucide-react'
import { eraseCode, loadCode, readCode, REPLAY_STATES, setCodeAutostart, storeCode } from './boardCode'
import { errorOwnerName, errorTagName } from './domain/decoder'
import { runitErrorCatalog } from './domain/descriptors'
import type { DeviceCatalog } from './domain/descriptors'
import type { ActionStep, ObjectSection, ProjectDevice, ProjectDocument, ProjectSettings } from './domain/project'
import { settingsState } from './domain/settings'
import { buildStoredCode, crc32, decodeFrameList, decodeStoredCode, encodeFrameList, serializeStoredCode } from './domain/storedCode'
import type { RecoveredCode } from './domain/storedCode'
import { BOARD_DEFAULT_SETTINGS, storedCodeContext } from './useBoardCode'
import type { BoardCodeState } from './useBoardCode'
import type { RunitBleSession } from './backend/runitBleSession'

/*
 * "Code is ready": the project built into the frames a board replays at boot
 * (app/docs/03_records_as_storage.md). Store it, restart the board with it,
 * read it back into a project (recovery), or export it (`runit-code`).
 */

const errors = runitErrorCatalog()
const hex8 = (value: number): string => `0x${value.toString(16).padStart(8, '0').toUpperCase()}`
const kb = (bytes: number): string => (bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`)

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
  /** Replace the project with code read from the board. */
  readonly onRecover: (recovered: RecoveredCode, autostart: boolean) => void
  readonly note: (ok: boolean, text: string) => void
}

export function StoredCodeSection({ project, sections, settings, devices, deviceCatalog, setup, board, session, maxFrameBytes, setAutostart, onRecover, note }: Props) {
  const [busy, setBusy] = useState<string>()
  const [progress, setProgress] = useState<{ done: number; total: number }>()
  const code = useMemo(
    () => {
      const context = storedCodeContext()
      return buildStoredCode({ project, sections, settings: settingsState(settings), boardDefaults: BOARD_DEFAULT_SETTINGS, devices, setup, extraFrames: project.extraFrames }, deviceCatalog ? { ...context, devices: deviceCatalog } : context)
    },
    [project, sections, settings, devices, deviceCatalog, setup],
  )
  const bytes = useMemo(() => encodeFrameList(code.steps.map((step) => step.frame)), [code])
  const crc = useMemo(() => crc32(bytes), [bytes])
  const autostart = project.autostart ?? false
  const { info } = board
  const schemaMismatch = info && info.firmwareSchemaId !== errors.schemaId
  const matches = info?.stored && info.crc32 === crc && info.schemaId === errors.schemaId
  const errorsOf = code.diagnostics.filter((entry) => entry.severity === 'error')

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

  const store = () => run('Store', async () => {
    if (!session) throw new Error('Connect a runIT board first.')
    const stored = await storeCode(session, bytes, errors.schemaId, maxFrameBytes, (done, total) => setProgress({ done, total }))
    if (stored.autostart !== autostart) await setCodeAutostart(session, autostart)
    board.refresh()
    return `Stored ${code.steps.length} frames (${kb(bytes.byteLength)}, CRC ${hex8(crc)}), autostart ${autostart ? 'on' : 'off'}. Load restarts the board with it.`
  })

  const load = () => {
    if (!window.confirm('Restart the board? It replays its stored code at boot; the connection drops and you reconnect afterwards.')) return
    void run('Load', async () => {
      if (!session) throw new Error('Connect a runIT board first.')
      await loadCode(session)
      return 'The board restarts with its stored code: reconnect, then check the replay report here.'
    })
  }

  const erase = () => {
    if (!window.confirm('Erase the stored code? The board keeps running until it restarts, then starts with its defaults.')) return
    void run('Erase', async () => {
      if (!session) throw new Error('Connect a runIT board first.')
      await eraseCode(session)
      board.refresh()
      return 'Stored code erased.'
    })
  }

  const recover = () => {
    if (!info?.stored) return
    if (!window.confirm('Replace this project (objects, devices, BLE and connector settings) with the code stored on the board? Save the project first if you want to keep it.')) return
    void run('Recover', async () => {
      if (!session) throw new Error('Connect a runIT board first.')
      const { bytes: stored } = await readCode(session, info, (done, total) => setProgress({ done, total }))
      const recovered = decodeStoredCode(decodeFrameList(stored), BOARD_DEFAULT_SETTINGS, storedCodeContext(), project.name || 'Recovered')
      onRecover(recovered, info.autostart)
      const warnings = recovered.diagnostics.length ? ` ${recovered.diagnostics.length} note(s): ${recovered.diagnostics.map((entry) => entry.message).join(' ')}` : ''
      return `Recovered ${info.frameCount} frames from the board.${warnings}`
    })
  }

  const exportCode = () => {
    const url = URL.createObjectURL(new Blob([serializeStoredCode({ schemaId: errors.schemaId, steps: code.steps })], { type: 'application/json' }))
    const link = document.createElement('a')
    link.href = url
    link.download = `${project.name || 'project'}.code.json`
    link.click()
    setTimeout(() => URL.revokeObjectURL(url), 0)
  }

  const replay = info?.replay
  const disabled = !session || !!busy

  return (
    <div className="object-details-section">
      <h3>Stored code</h3>
      <p className="program-muted">What the board replays at every boot. Live uploads and Apply change the running board only; store to keep them.</p>
      <dl className="program-facts">
        <div><dt>Project code</dt><dd>{code.ok ? `${code.steps.length} frames · ${kb(bytes.byteLength)} · ${hex8(crc)}` : 'has errors'}</dd></div>
        {session && info && (
          <>
            <div><dt>On the board</dt><dd>{info.stored ? `${info.frameCount} frames · ${kb(info.length)} / ${kb(info.capacity)} · ${hex8(info.crc32)}` : 'none'}</dd></div>
            <div><dt>Board autostart</dt><dd>{info.autostart ? 'on' : 'off'}</dd></div>
            {replay && <div><dt>Last boot</dt><dd>{REPLAY_STATES[replay.state] ?? `state ${replay.state}`}{replay.state === 1 ? ` · ${replay.applied} applied${replay.failed ? `, ${replay.failed} refused` : ''}` : ''}</dd></div>}
          </>
        )}
        {session && board.reading && <div><dt>On the board</dt><dd>reading…</dd></div>}
      </dl>
      {errorsOf.map((entry, index) => <p key={`e${index}`} className="program-diag is-error"><AlertCircle aria-hidden="true" />{entry.message}</p>)}
      {schemaMismatch && <p className="program-diag is-error"><AlertCircle aria-hidden="true" />The board's firmware (schema {hex8(info.firmwareSchemaId)}) differs from the app's descriptors ({hex8(errors.schemaId)}): it refuses this code.</p>}
      {info && replay && replay.firstFailed !== undefined && (
        <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />Boot replay refused frame {replay.firstFailed} first: {errorTagName(errors, replay.firstTag)} ({errorOwnerName(errors, replay.firstOwner)}).</p>
      )}
      {info && code.ok && !schemaMismatch && (matches
        ? <p className="program-diag"><CheckCircle2 aria-hidden="true" />The board stores this project's code.</p>
        : <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />{info.stored ? 'The code on the board differs from this project.' : 'No code stored on the board.'}</p>)}
      {board.note && <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />{board.note}</p>}
      <label className="program-check">
        <input type="checkbox" checked={autostart} onChange={(event) => setAutostart(event.target.checked)} />
        <span>Run the program after boot (autostart)</span>
      </label>
      <div className="program-actions">
        <button className="program-primary" disabled={disabled || !code.ok || !!schemaMismatch} onClick={() => void store()} title="Write the project's code to the board (replaces its stored code)">
          <HardDriveUpload aria-hidden="true" /><span>{busy === 'Store' && progress ? `${progress.done}/${progress.total}` : 'Store'}</span>
        </button>
        <button disabled={disabled} onClick={load} title="Restart the board: it replays its stored code"><RotateCcw aria-hidden="true" /><span>Load</span></button>
        <button disabled={disabled || !info?.stored} onClick={recover} title="Read the board's code back into this project">
          <HardDriveDownload aria-hidden="true" /><span>{busy === 'Recover' && progress ? `${Math.round((100 * progress.done) / Math.max(1, progress.total))}%` : 'Recover'}</span>
        </button>
        <button disabled={disabled || !info?.stored} onClick={erase} title="Erase the stored code"><Trash2 aria-hidden="true" /><span>Erase</span></button>
        <button disabled={!code.ok} onClick={exportCode} title="Save the code as JSON (runit-code): frames as hex, CRC, schema ID"><Download aria-hidden="true" /><span>Export</span></button>
      </div>
    </div>
  )
}
