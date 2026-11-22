import { useCallback, useEffect, useMemo, useState } from 'react'
import { FilePlus2, FolderOpen, History, RefreshCw, Save } from 'lucide-react'
import { bundledServiceCatalog, listRecords } from '@backend'
import type { RunitLink } from '@backend'
import { Badge, Button, Card, CardStack, EditableField, FieldError, FormGrid, FormRow, PanelHeader } from '@ui/components'
import { BOARD_RECORD_ID } from '../devices/boardProfile'
import { FirmwareCard } from './FirmwareCard'
import { projectCounts } from './projectFile'
import type { useProject } from './useProject'

type ProjectState = ReturnType<typeof useProject>

/** What the board holds: the records stored in its flash (SYS_ACTIONS.MD) and whether it keeps a profile (SYS_BOARD.MD). */
function BoardCard({ link, connected }: { link: RunitLink; connected: boolean }) {
  const catalog = useMemo(() => bundledServiceCatalog(), [])
  const [records, setRecords] = useState<readonly number[] | undefined>()
  const [error, setError] = useState<string | undefined>()
  const refresh = useCallback(async () => {
    setError(undefined)
    try {
      setRecords(await listRecords(link, catalog))
    } catch (failure) {
      setRecords(undefined)
      setError(failure instanceof Error ? failure.message : String(failure))
    }
  }, [link, catalog])
  useEffect(() => {
    setRecords(undefined)
    if (connected) void refresh()
  }, [connected, refresh])

  if (!connected) return <Card title="On the board" subhead="Connect the board to see what it keeps." />
  return (
    <Card title="On the board" subhead="The records the board keeps in its memory. It keeps no copy of the project: Run loads its program into the VM.">
      <FormGrid>
        <FormRow label="Records">
          <span>{records ? records.length : '…'}</span>
        </FormRow>
        <FormRow label="Board profile">
          <span>{records ? (records.includes(BOARD_RECORD_ID) ? 'Stored' : 'None') : '…'}</span>
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
 * The Project screen: the file (name, New, Open, Save, Restore previous), what the project holds and what the board holds; the
 * services' settings are tabs of their own (settings/SettingsTabs.tsx).
 */
export function ProjectScreen({ project, link, connected }: { project: ProjectState; link: RunitLink; connected: boolean }) {
  const counts = projectCounts(project.project)
  const { note } = project
  return (
    <div className="project-screen">
      <PanelHeader title={project.project.name}>
        {project.unsaved ? <Badge tone="warning">unsaved</Badge> : <Badge tone="success">saved</Badge>}
      </PanelHeader>
      <CardStack>
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
        <BoardCard link={link} connected={connected} />
        <FirmwareCard connected={connected} />
      </CardStack>
    </div>
  )
}
