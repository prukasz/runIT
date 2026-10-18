import { useCallback, useEffect, useState } from 'react'
import { FilePlus2, FolderOpen, History, RefreshCw, Save } from 'lucide-react'
import { bundledEnums, projectInfo } from '@backend'
import type { ProjectInfo, RunitLink } from '@backend'
import { Badge, Button, Card, CardStack, EditableField, FieldError, FormGrid, FormRow, PanelHeader } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import type { BoardRun } from '../useVm'
import type { PinContext } from '../devices/FieldsForm'
import { SettingsCards } from '../settings/SettingsScreen'
import type { BoardSettingsState } from '../settings/useBoardSettings'
import { projectCounts } from './projectFile'
import type { useProject } from './useProject'

type ProjectState = ReturnType<typeof useProject>

/** The alias of a `vm_run_mode_e` member (`Running`), from the published enums. */
const modeAlias = (name: string): string => bundledEnums().vm_run_mode_e?.members.find((m) => m.name === name)?.alias ?? name

/** What the board holds: the project stored in its flash (`PROJECT INFO`) and the program in its VM (`VM STATUS`). */
function BoardCard({ link, connected, run }: { link: RunitLink; connected: boolean; run?: BoardRun }) {
  const [info, setInfo] = useState<ProjectInfo | undefined>()
  const [error, setError] = useState<string | undefined>()
  const refresh = useCallback(async () => {
    setError(undefined)
    try {
      setInfo(await projectInfo(link))
    } catch (failure) {
      setInfo(undefined)
      setError(failure instanceof Error ? failure.message : String(failure))
    }
  }, [link])
  useEffect(() => {
    setInfo(undefined)
    if (connected) void refresh()
  }, [connected, refresh])

  if (!connected) return <Card title="On the board" subhead="Connect the board to see the project it keeps and the program it runs." />
  return (
    <Card title="On the board" subhead="The project the board keeps in its memory, and the program in its VM now.">
      <FormGrid>
        <FormRow label="Stored project">
          {info ? <span>{info.stored ? `Yes, ${info.bytes} bytes` : 'None'}</span> : <span>…</span>}
        </FormRow>
        <FormRow label="Program in the VM">
          {run ? <span>{run.ready ? 'Loaded' : 'None'}</span> : <span>…</span>}
        </FormRow>
        <FormRow label="Program state">
          {run
            ? <span>{modeAlias(run.mode)}{run.mode === 'VM_RUN_RUNNING' ? ` · ${run.passes} passes` : ''} {run.faultLatched && <Badge tone="danger">fault</Badge>}</span>
            : <span>…</span>}
        </FormRow>
      </FormGrid>
      <FieldError>{error}</FieldError>
      <div className="project-actions">
        <Button size="sm" onClick={() => void refresh()}><RefreshCw aria-hidden="true" />Refresh</Button>
      </div>
    </Card>
  )
}

/**
 * The Project screen: the file (name, New, Open, Save, Restore previous), what the project holds, what the board holds, and the
 * project service's own settings (autostart); the other services' settings are tabs of their own (settings/SettingsTabs.tsx).
 */
export function ProjectScreen({ project, link, connected, run, settings, context, nameOf }: { project: ProjectState; link: RunitLink; connected: boolean; run?: BoardRun; settings: BoardSettingsState; context: PinContext; nameOf: (device: number) => string }) {
  // The project service's own settings (autostart): the other services' are tabs of their own.
  const own = settings.sections.find((s) => s.service.key === link.defs.keys.project)
  const counts = projectCounts(project.project)
  const { note } = project
  return (
    <div className="project-screen">
      <PanelHeader title={project.project.name}>
        {project.unsaved ? <Badge tone="warning">unsaved</Badge> : <Badge tone="success">saved</Badge>}
      </PanelHeader>
      <CardStack>
        {link.defs.keys.project !== undefined && <ErrorsCard about={`router_key:${link.defs.keys.project}`} title="Errors of the stored project" />}
        <Card title="Project file" subhead="The project is kept in this app between sessions; Save writes it to a file you can open again or share.">
          <FormRow label="Name">
            <EditableField value={project.project.name} maxLength={64} aria-label="Project name" onChange={(value) => project.rename(value)} />
          </FormRow>
          <div className="project-actions">
            <Button onClick={project.newProject}><FilePlus2 aria-hidden="true" />New</Button>
            <Button onClick={project.open}><FolderOpen aria-hidden="true" />Open…</Button>
            <Button variant="primary" onClick={project.save}><Save aria-hidden="true" />Save</Button>
            {project.previous && <Button variant="dashed" onClick={project.restorePrevious}><History aria-hidden="true" />Restore {project.previous.name}</Button>}
          </div>
          {note && (note.tone === 'error'
            ? <FieldError>{note.text}</FieldError>
            : <p className="project-note">{note.text}</p>)}
          {note?.problems && <ul className="project-problems">{note.problems.map((problem, i) => <li key={i}>{problem}</li>)}</ul>}
        </Card>
        <Card title="Contents">
          <div className="project-counts">
            <Badge tone="count">{counts.variables}</Badge> variables
            <Badge tone="count">{counts.blocks}</Badge> blocks
            <Badge tone="count">{counts.devices}</Badge> devices
          </div>
        </Card>
        <BoardCard link={link} connected={connected} run={run} />
        {own && <SettingsCards section={own} state={settings} context={context} nameOf={nameOf} />}
      </CardStack>
    </div>
  )
}
