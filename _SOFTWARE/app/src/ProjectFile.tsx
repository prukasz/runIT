import { useMemo, useState } from 'react'
import { AlertCircle, CheckCircle2, Download, FilePlus2, FileDown, FileUp, FolderOpen, HardDriveDownload, Sparkles, Undo2, Upload } from 'lucide-react'
import { Button } from './components/Button'
import { Card, CardStack } from './components/Card'
import { TextField } from './components/FormField'
import { PanelHeader } from './components/PanelHeader'
import { readCode } from './boardCode'
import { recoveredCanvases } from './canvas'
import { runitErrorCatalog } from './domain/descriptors'
import type { DeviceCatalog } from './domain/descriptors'
import type { ActionStep, ProjectCanvas, ProjectDevice, ProjectSettings } from './domain/project'
import { settingsFromState } from './domain/settings'
import { createProject } from './domain/project'
import { decodeFrameList, decodeStoredCode, parseStoredCode, serializeStoredCode } from './domain/storedCode'
import type { RecoveredCode } from './domain/storedCode'
import type { ObjectWorkspace } from './ObjectTreeWorkspace'
import { BOARD_DEFAULT_SETTINGS, storedCodeContext } from './useBoardCode'
import type { BoardCodeState } from './useBoardCode'
import { useProjectCode } from './useProjectCode'
import type { RunitBleSession } from './backend/runitBleSession'
import './ProjectFile.css'

/*
 * The Home view: the project as a file (new, open, save), its code as a file
 * (import, export: `runit-code`), and the code stored on a board (recover).
 * The Run view keeps what changes the board: store, load, erase.
 */

const errors = runitErrorCatalog()
const hex8 = (value: number): string => `0x${value.toString(16).padStart(8, '0').toUpperCase()}`
const kb = (bytes: number): string => (bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KB`)

interface Data {
  readonly workspace: ObjectWorkspace
  readonly settings: ProjectSettings
  readonly devices: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
  readonly setup: readonly ActionStep[]
  readonly canvases: readonly ProjectCanvas[]
}

export interface ProjectFileProps extends Data {
  readonly board: BoardCodeState
  readonly session?: RunitBleSession
  readonly onNew: () => void
  readonly onOpen: (file: File | undefined) => void
  readonly onSave: () => void
  /** Replace the project with code read from a board or a code file. */
  readonly onRecover: (recovered: RecoveredCode, autostart: boolean) => void
  /** A replaced project is kept aside; Restore previous brings it back. */
  readonly canRestore: boolean
  readonly onRestore: () => void
  /** Replace the project with the sample program. */
  readonly onSample: () => void
}

/** Left panel: the actions. */
export function ProjectFilePalette({ workspace, settings, devices, deviceCatalog, setup, canvases, board, session, onNew, onOpen, onSave, onRecover, canRestore, onRestore, onSample }: ProjectFileProps) {
  const project = useMemo(() => ({ ...workspace.project, canvases }), [workspace.project, canvases])
  const { code } = useProjectCode({ project, sections: workspace.sections, settings, devices, deviceCatalog, setup })
  const [busy, setBusy] = useState<string>()
  const [progress, setProgress] = useState<{ done: number; total: number }>()
  const [result, setResult] = useState<{ ok: boolean; text: string }>()
  const { info } = board

  const run = async (label: string, operation: () => Promise<string>) => {
    setBusy(label)
    setResult(undefined)
    try {
      setResult({ ok: true, text: await operation() })
    } catch (error) {
      setResult({ ok: false, text: `${label}: ${error instanceof Error ? error.message : String(error)}` })
    } finally {
      setBusy(undefined)
      setProgress(undefined)
    }
  }
  const summary = (recovered: RecoveredCode, frames: number) => {
    const notes = recovered.diagnostics.length ? ` ${recovered.diagnostics.length} note(s): ${recovered.diagnostics.map((entry) => entry.message).join(' ')}` : ''
    return `Read ${frames} frames.${notes}`
  }

  const importCode = (file: File | undefined) => {
    if (!file) return
    void run('Import', async () => {
      const parsed = parseStoredCode(await file.text())
      if (parsed.schemaId !== errors.schemaId) throw new Error(`the file was made for another firmware (schema ${hex8(parsed.schemaId)}; this app knows ${hex8(errors.schemaId)}).`)
      const recovered = decodeStoredCode(parsed.steps.map((step) => step.frame), BOARD_DEFAULT_SETTINGS, storedCodeContext(), project.name || 'Imported')
      onRecover(recovered, recovered.project.autostart ?? false)
      return summary(recovered, parsed.steps.length)
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
  const recover = () => {
    if (!info?.stored || !session) return
    void run('Recover', async () => {
      const { bytes: stored } = await readCode(session, info, (done, total) => setProgress({ done, total }))
      const recovered = decodeStoredCode(decodeFrameList(stored), BOARD_DEFAULT_SETTINGS, storedCodeContext(), project.name || 'Recovered')
      onRecover(recovered, info.autostart)
      return `Recovered from the board. ${summary(recovered, info.frameCount)}`
    })
  }

  return (
    <div className="file-panel">
      <div className="file-group">
        <h3>Project file</h3>
        <Button block onClick={onNew} title="Start an empty project"><FilePlus2 aria-hidden="true" /><span>New project</span></Button>
        <label className="ui-button is-md is-default is-block" title="Open a project file (.json)">
          <FolderOpen aria-hidden="true" />
          <span>Open…</span>
          <TextField type="file" accept=".json,application/json" hidden onChange={(event) => { onOpen(event.target.files?.[0]); event.target.value = '' }} />
        </label>
        <Button block onClick={onSave} title="Download the project as a file (it is also saved in the browser as you work)"><FileDown aria-hidden="true" /><span>Download</span></Button>
        <Button block onClick={onSample} title="Load a small working program to try the app with"><Sparkles aria-hidden="true" /><span>Sample program</span></Button>
        <Button block disabled={!canRestore} onClick={onRestore} title="Bring back the project that New, Open, Import or Recover replaced"><Undo2 aria-hidden="true" /><span>Restore previous</span></Button>
      </div>
      <div className="file-group">
        <h3>Code file</h3>
        <p className="file-hint">The program as the frames a board replays at boot (runit-code).</p>
        <label className={`ui-button is-md is-default is-block ${busy ? 'is-busy' : ''}`} title="Read a code file into a new project">
          <FileUp aria-hidden="true" />
          <span>Import…</span>
          <TextField type="file" accept=".json,application/json" hidden disabled={!!busy} onChange={(event) => { importCode(event.target.files?.[0]); event.target.value = '' }} />
        </label>
        <Button block disabled={!code.ok} onClick={exportCode} title="Save the code as JSON: frames as hex, CRC, schema ID"><Download aria-hidden="true" /><span>Export</span></Button>
      </div>
      <div className="file-group">
        <h3>Board</h3>
        <p className="file-hint">{session ? (info?.stored ? `The board stores ${info.frameCount} frames.` : 'The board stores no code.') : 'Connect a board to read its code.'}</p>
        <Button block disabled={!session || !!busy || !info?.stored} onClick={recover} title="Read the board's code back into this project">
          <HardDriveDownload aria-hidden="true" /><span>{busy === 'Recover' && progress ? `Recovering ${Math.round((100 * progress.done) / Math.max(1, progress.total))}%` : 'Recover from board'}</span>
        </Button>
      </div>
      {busy && !progress && <p className="file-hint">{busy}…</p>}
      {result && <p className={`program-diag ${result.ok ? '' : 'is-error'}`}>{result.ok ? <CheckCircle2 aria-hidden="true" /> : <AlertCircle aria-hidden="true" />}{result.text}</p>}
    </div>
  )
}

/** Main screen: what the project holds and the code it builds. */
export function ProjectFilePage({ workspace, settings, devices, deviceCatalog, setup, canvases, onRecover }: Data & Pick<ProjectFileProps, 'onRecover'>) {
  const project = useMemo(() => ({ ...workspace.project, canvases }), [workspace.project, canvases])
  const { code, bytes, crc } = useProjectCode({ project, sections: workspace.sections, settings, devices, deviceCatalog, setup })
  const objects = (nodes: typeof project.objects): number => nodes.reduce((sum, node) => sum + 1 + (node.kind === 'folder' ? objects(node.children) : 0), 0)
  const blocks = canvases.reduce((sum, canvas) => sum + canvas.blocks.length, 0)
  const problems = code.diagnostics.filter((entry) => entry.severity === 'error')
  return (
    <div className="file-page">
      <PanelHeader icon={<Upload aria-hidden="true" />} title={project.name || 'Untitled'} />
      <CardStack>
        <Card title="Project">
          <dl className="program-facts">
            <div><dt>Objects</dt><dd>{objects(project.objects)}</dd></div>
            <div><dt>Devices</dt><dd>{devices.length}</dd></div>
            <div><dt>Canvases</dt><dd>{canvases.length} · {blocks} blocks</dd></div>
            <div><dt>Setup steps</dt><dd>{setup.length}</dd></div>
          </dl>
        </Card>
        <Card title="Code">
          <dl className="program-facts">
            <div><dt>Frames</dt><dd>{code.ok ? code.steps.length : 'has errors'}</dd></div>
            <div><dt>Size</dt><dd>{kb(bytes.byteLength)}</dd></div>
            <div><dt>CRC</dt><dd>{hex8(crc)}</dd></div>
          </dl>
          {problems.map((entry, index) => <p key={index} className="program-diag is-error"><AlertCircle aria-hidden="true" />{entry.message}</p>)}
        </Card>
        <RecoveryPreview frames={code.ok ? code.steps.map((step) => step.frame) : undefined} crc={crc} name={project.name} settings={settings} deviceCatalog={deviceCatalog} onRecover={onRecover} />
      </CardStack>
    </div>
  )
}

/** The project's own code read back the way a board's would be: what returns, what doesn't, and whether it builds the same frames. */
function RecoveryPreview({ frames, crc, name, settings, deviceCatalog, onRecover }: {
  readonly frames: readonly Uint8Array[] | undefined
  readonly crc: number
  readonly name: string
  readonly settings: ProjectSettings
  readonly deviceCatalog?: DeviceCatalog
  readonly onRecover: ProjectFileProps['onRecover']
}) {
  const recovered = useMemo(() => (frames ? decodeStoredCode(frames, BOARD_DEFAULT_SETTINGS, storedCodeContext(), name || 'Recovered') : undefined), [frames, name])
  const project = useMemo(() => (recovered ? { ...recovered.project, canvases: recoveredCanvases(recovered.blocks) } : undefined), [recovered])
  const again = useProjectCode({
    project: project ?? { ...createProject('') },
    sections: recovered?.sections ?? [],
    settings: recovered ? settingsFromState(recovered.settings, settings) : settings,
    devices: recovered?.devices ?? [],
    deviceCatalog,
    setup: recovered?.setup ?? [],
  })
  if (!recovered || !project) return <Card title="Recovery preview"><p className="file-hint">Fix the code errors above to preview what recovery would return.</p></Card>
  const count = (nodes: typeof project.objects): number => nodes.reduce((sum, node) => sum + 1 + (node.kind === 'folder' ? count(node.children) : 0), 0)
  const same = again.code.ok && again.crc === crc
  return (
    <Card title="Recovery preview">
      <p className="file-hint">This project's code decoded the way a board's would be. Names come only from stored variables; blocks get generated IDs and an automatic layout.</p>
      <dl className="program-facts">
        <div><dt>Objects</dt><dd>{count(project.objects)}{recovered.sections.length ? ` + ${recovered.sections.reduce((sum, section) => sum + count(section.objects), 0)} block-owned` : ''}</dd></div>
        <div><dt>Blocks</dt><dd>{recovered.blocks.length ? recovered.blocks.map((block) => block.id).join(', ') : 'none'}</dd></div>
        <div><dt>Rebuilt code</dt><dd>{again.code.ok ? `${again.code.steps.length} frames · ${hex8(again.crc)}` : 'has errors'}</dd></div>
      </dl>
      {same
        ? <p className="program-diag"><CheckCircle2 aria-hidden="true" />The recovered project builds the same code (same CRC).</p>
        : <p className="program-diag is-warning"><AlertCircle aria-hidden="true" />The recovered project builds different code (CRC {hex8(again.crc)} vs {hex8(crc)}).</p>}
      {recovered.diagnostics.map((entry, index) => <p key={index} className={`program-diag ${entry.severity === 'error' ? 'is-error' : 'is-warning'}`}><AlertCircle aria-hidden="true" />{entry.message}</p>)}
      <Button onClick={() => onRecover(recovered, recovered.project.autostart ?? false)} title="Load the recovered project (the current one is kept: Restore previous)">
        <HardDriveDownload aria-hidden="true" /><span>Open recovered</span>
      </Button>
    </Card>
  )
}
