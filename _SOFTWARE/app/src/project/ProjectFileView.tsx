import { AlertCircle, CheckCircle2, FileDown, FilePlus2, FolderOpen, Undo2 } from 'lucide-react'
import { Button, buttonClass } from '../components/Button'
import { Card, CardStack } from '../components/Card'
import { EditableField } from '../components/EditableField'
import { PanelHeader } from '../components/PanelHeader'
import { walkObjects } from '../domain/project'
import { SettingsCategories } from '../settings/SettingsOverview'
import type { SettingsGroup } from '../settings/SettingsOverview'
import type { ProjectFile } from './useProjectFile'
import './ProjectFile.css'

/*
 * The Project tab: the project as a file on the left (New, Open, Download, Restore previous, as in the legacy Home view) with the
 * settings categories under it, and in the middle what the project holds until a category is picked.
 */

/** Left panel: the file actions, then the settings categories. */
export function ProjectFilePalette({ file, onPickSettings }: { file: ProjectFile; onPickSettings: (group: SettingsGroup) => void }) {
  return (
    <div className="file-panel">
      <div className="file-group">
        <h3>Project file</h3>
        <Button block onClick={file.newProject} title="Start an empty project (the current one can be restored)"><FilePlus2 aria-hidden="true" /><span>New project</span></Button>
        <label className={buttonClass({ block: true })} title="Open a project file (.json)">
          <FolderOpen aria-hidden="true" />
          <span>Open…</span>
          <input type="file" accept=".json,application/json" hidden onChange={(event) => { void file.open(event.target.files?.[0]); event.target.value = '' }} />
        </label>
        <Button block onClick={file.download} title="Download the project as a file (it is also kept in the browser as you work)"><FileDown aria-hidden="true" /><span>Download</span></Button>
        <Button block disabled={!file.canRestore} onClick={file.restore} title={file.canRestore ? `Bring back “${file.previousName || 'Untitled'}”, the project New or Open replaced` : 'Nothing to restore yet'}>
          <Undo2 aria-hidden="true" /><span>Restore previous</span>
        </Button>
        {file.result && <p className={`file-result ${file.result.ok ? '' : 'is-error'}`}>{file.result.ok ? <CheckCircle2 aria-hidden="true" /> : <AlertCircle aria-hidden="true" />}{file.result.text}</p>}
      </div>
      <SettingsCategories onPick={onPickSettings} />
    </div>
  )
}

/** Middle: the project's name and what it holds. */
export function ProjectFilePage({ file }: { file: ProjectFile }) {
  const bundle = file.current()
  let objects = 0
  walkObjects(bundle.project.objects, () => { objects++ })
  const blocks = bundle.canvases.reduce((sum, canvas) => sum + canvas.blocks.length, 0)
  const services = bundle.settings.ble.services.filter((service) => !service.system)
  const streams = bundle.settings.connectors.filter((connector) => !connector.system)
  return (
    <div className="file-page">
      <PanelHeader icon={<FolderOpen aria-hidden="true" />} title={<EditableField value={file.name} placeholder="Untitled" aria-label="Project name" onChange={(value) => file.rename(value)} />} />
      <CardStack>
        <Card title="Project">
          <dl className="program-facts">
            <div><dt>Objects</dt><dd>{objects}</dd></div>
            <div><dt>Canvases</dt><dd>{bundle.canvases.length} · {blocks} blocks</dd></div>
            <div><dt>User devices</dt><dd>{bundle.devices.list.length}</dd></div>
            <div><dt>Actions</dt><dd>{bundle.devices.actions.length} · {bundle.devices.setup.length} default settings</dd></div>
          </dl>
        </Card>
        <Card title="Settings">
          <dl className="program-facts">
            <div><dt>Bluetooth services</dt><dd>{services.length} · {services.reduce((sum, service) => sum + service.characteristics.length, 0)} characteristics</dd></div>
            <div><dt>User data streams</dt><dd>{streams.length}</dd></div>
          </dl>
          <p className="file-hint">Pick a category on the left to edit the settings. The toolbar sends them to the board (and reads back what it stores).</p>
        </Card>
      </CardStack>
    </div>
  )
}
