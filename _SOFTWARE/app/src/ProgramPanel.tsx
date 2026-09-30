import { useEffect, useMemo, useState } from 'react'
import { PanelHeader } from './components/PanelHeader'
import { Button } from './components/Button'
import { TextField } from './components/FormField'
import { AlertCircle, AlertTriangle, Pause, Play, Upload } from 'lucide-react'
import { packExec } from './domain/compiler'
import type { ObjectLayout } from './domain/compiler'
import { applyVmValues, decodeVmElements, decodeVmTelemetry } from './domain/decoder'
import { runitVmCatalog } from './domain/descriptors'
import type { DeviceCatalog } from './domain/descriptors'
import { serializeProject } from './domain/project'
import { nameIds } from './domain/canvas'
import type { ActionStep, ProjectCanvas, ProjectDevice, ProjectSettings } from './domain/project'
import { planVmUpload } from './domain/upload'
import { StoredCodeSection } from './StoredCodeSection'
import type { BoardCodeState } from './useBoardCode'
import { sendSteps } from './sendSteps'
import { DEFAULT_MAX_FRAME_BYTES, MAX_FRAME_BYTES } from './frameLimits'
import type { RunitBleSession } from './backend/runitBleSession'
import type { BleDeviceConnection } from './useBleDeviceConnection'
import type { ObjectWorkspace } from './ObjectTreeWorkspace'

const catalog = runitVmCatalog()

interface Uploaded {
  /** The session it went to: a new connection knows nothing about it. */
  readonly session: RunitBleSession
  readonly layout: ObjectLayout
  /** Project + sections as uploaded, to tell when the editor moved on. */
  readonly source: string
}

const formatValue = (value: ReturnType<typeof decodeVmElements>): string =>
  typeof value === 'string' ? JSON.stringify(value) : value.map((entry) => (typeof entry === 'number' && !Number.isInteger(entry) ? entry.toPrecision(6) : String(entry))).join(' ')

interface Props {
  readonly workspace: ObjectWorkspace
  readonly connection: BleDeviceConnection
  readonly board: BoardCodeState
  readonly settings: ProjectSettings
  readonly devices: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
  readonly setup: readonly ActionStep[]
  /** The canvases: their blocks are the program. */
  readonly canvases: readonly ProjectCanvas[]
}

export function ProgramPanel({ workspace: w, connection, board, settings, devices, deviceCatalog, setup, canvases }: Props) {
  const project = useMemo(() => ({ ...w.project, canvases }), [w.project, canvases])
  const [maxFrameBytes, setMaxFrameBytes] = useState(DEFAULT_MAX_FRAME_BYTES)
  const [busy, setBusy] = useState(false)
  const [progress, setProgress] = useState<{ done: number; total: number }>()
  const [log, setLog] = useState<{ ok: boolean; text: string }[]>([])
  const [lastUpload, setUploaded] = useState<Uploaded>()
  const [live, setLive] = useState<ReadonlyMap<number, Uint8Array>>(new Map())
  const { session } = connection
  const uploaded = lastUpload && lastUpload.session === session ? lastUpload : undefined

  const plan = useMemo(() => planVmUpload(project, catalog, { maxFrameBytes, sections: w.sections }), [project, w.sections, maxFrameBytes])
  const source = useMemo(() => JSON.stringify([serializeProject(project), w.sections]), [project, w.sections])
  const errors = plan.diagnostics.filter((entry) => entry.severity === 'error')
  const warnings = plan.diagnostics.filter((entry) => entry.severity === 'warning')
  const note = (ok: boolean, text: string) => setLog((entries) => [{ ok, text: `${new Date().toLocaleTimeString()} ${text}` }, ...entries].slice(0, 30))

  useEffect(() => {
    if (!session || !uploaded) return undefined
    const sizeOf = (id: number) => {
      const placed = uploaded.layout.objects[id]
      return placed && { bytes: placed.elements * placed.type.wireWidth, wireWidth: placed.type.wireWidth }
    }
    return session.received.subscribe((frame) => {
      try {
        const decoded = decodeVmTelemetry(frame.data, catalog)
        if (decoded?.kind === 'values') setLive((values) => applyVmValues(values, decoded.records, sizeOf))
      } catch (error) {
        note(false, `telemetry: ${error instanceof Error ? error.message : String(error)}`)
      }
    })
  }, [session, uploaded])

  const run = async (label: string, operation: () => Promise<void>) => {
    setBusy(true)
    try {
      await operation()
      note(true, label)
    } catch (error) {
      note(false, error instanceof Error ? error.message : String(error))
    } finally {
      setBusy(false)
      setProgress(undefined)
    }
  }

  const upload = () => run(`Uploaded ${plan.program.counts.objects} objects in ${plan.steps.length} frames, subscribed to ${plan.subscribed.length}.`, async () => {
    if (!session) throw new Error('Connect a runIT board first.')
    setUploaded(undefined)
    setLive(new Map())
    await sendSteps(session, plan.steps, (done, total) => setProgress({ done, total }))
    setUploaded({ session, layout: plan.program.objects, source })
  })

  const exec = (member: string, label: string) => run(label, async () => {
    if (!session) throw new Error('Connect a runIT board first.')
    const command = catalog.execCommands.get(member)
    if (command === undefined) throw new Error(`The firmware has no VM_EXEC_${member}.`)
    await sendSteps(session, [{ label: `vm exec ${member.toLowerCase()}`, frame: packExec(catalog, command) }])
  })

  const liveRows = uploaded ? uploaded.layout.objects.filter((entry) => entry.node.kind === 'value' && live.has(entry.wireId)) : []

  return (
    <div className="program-panel">
      <PanelHeader icon={<span className="object-type-icon text"><Upload aria-hidden="true" /></span>} title="Program" />

      <div className="object-details-section">
        <h3>Build</h3>
        <dl className="program-facts">
          {plan.program.objects.sections.map((section) => (
            <div key={section.key}><dt>{section.key === 'user' ? 'Objects' : `Section ${section.key}`}</dt><dd>{section.count} · IDs {section.count ? `${section.first}–${section.first + section.count - 1}` : '–'}</dd></div>
          ))}
          <div><dt>Arena</dt><dd>{plan.program.arenaBytes} / {catalog.arena.maxBytes} B</dd></div>
          {plan.program.retainBytes > 0 && <div><dt>Retained</dt><dd>{plan.program.retainBytes} / {catalog.retainMaxBytes} B</dd></div>}
          <div><dt>Frames</dt><dd>{plan.steps.length}</dd></div>
          <div><dt>Subscribed</dt><dd>{plan.subscribed.length}</dd></div>
          <div>
            <dt>Frame size</dt>
            <dd><TextField type="number" min={16} max={MAX_FRAME_BYTES} value={maxFrameBytes} onChange={(event) => setMaxFrameBytes(Math.max(16, Math.min(MAX_FRAME_BYTES, Math.floor(Number(event.target.value)) || DEFAULT_MAX_FRAME_BYTES)))} /> B</dd>
          </div>
        </dl>
        {errors.map((entry, index) => <p key={`e${index}`} className="program-diag is-error"><AlertCircle aria-hidden="true" />{nameIds(entry.message, project)}</p>)}
        {warnings.map((entry, index) => <p key={`w${index}`} className="program-diag is-warning"><AlertTriangle aria-hidden="true" />{nameIds(entry.message, project)}</p>)}
      </div>

      <div className="object-details-section">
        <h3>Device</h3>
        {!session && <p className="program-muted">{connection.isConnected ? 'Connected, but the board has no runIT command channel.' : 'Connect a board to upload.'}</p>}
        {uploaded && uploaded.source !== source && <p className="program-diag is-warning"><AlertTriangle aria-hidden="true" />The project changed since the upload.</p>}
        <div className="program-actions">
          <Button variant="primary" disabled={!session || !plan.ok || !plan.steps.length || busy} onClick={() => void upload()} title={!plan.ok ? 'Fix the errors first' : plan.steps.length ? 'Load the program (stops a running one first)' : 'The program is empty'}>
            <Upload aria-hidden="true" /><span>{progress ? `${progress.done}/${progress.total}` : 'Upload'}</span>
          </Button>
          <Button disabled={!session || !uploaded || busy} onClick={() => void exec('NORMAL_MODE', 'Program running.')}><Play aria-hidden="true" /><span>Run</span></Button>
          <Button disabled={!session || !uploaded || busy} onClick={() => void exec('PAUSE', 'Program paused.')}><Pause aria-hidden="true" /><span>Pause</span></Button>
        </div>
      </div>

      <StoredCodeSection
        project={project}
        sections={w.sections}
        settings={settings}
        devices={devices}
        deviceCatalog={deviceCatalog}
        setup={setup}
        board={board}
        session={session}
        maxFrameBytes={maxFrameBytes}
        setAutostart={w.setAutostart}
        note={note}
      />

      {liveRows.length > 0 && (
        <div className="object-details-section">
          <h3>Live values</h3>
          <dl className="program-facts">
            {liveRows.map((entry) => <div key={entry.wireId}><dt>{entry.node.name || `#${entry.wireId}`}</dt><dd>{formatValue(decodeVmElements(entry.type, live.get(entry.wireId)!))}</dd></div>)}
          </dl>
        </div>
      )}

      {log.length > 0 && (
        <div className="object-details-section">
          <h3>Log</h3>
          <ul className="program-log">{log.map((entry, index) => <li key={index} className={entry.ok ? '' : 'is-error'}>{entry.text}</li>)}</ul>
        </div>
      )}
    </div>
  )
}
