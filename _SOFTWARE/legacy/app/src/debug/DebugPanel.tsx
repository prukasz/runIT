import { useMemo, useState } from 'react'
import { AlertCircle, Bug, Eye, EyeOff, Pause, Play, SkipForward, StepForward } from 'lucide-react'
import { Button } from '../components/Button'
import { PanelHeader } from '../components/PanelHeader'
import { TextField } from '../components/FormField'
import { formatLiveValue } from '../domain/compiler'
import { walkObjects } from '../domain/project'
import type { ProjectDocument } from '../domain/project'
import { useDebug } from './DebugContext'
import type { RunMode } from './DebugContext'

/*
 * The right-hand Debug tab: start / stop the debug build, run it (continuously,
 * pass by pass, block by block) and choose which variables to watch, live.
 */

const MODE_TEXT: Readonly<Record<RunMode, string>> = {
  running: 'Running',
  paused: 'Paused',
  scan: 'Stepping by pass: each Step pass runs one full pass',
  block: 'Stepping by block: each Next block runs one block',
}

/** Most rows the watch list draws (filter to reach the rest). */
const MAX_ROWS = 200

export function DebugPanel({ project }: { readonly project: ProjectDocument }) {
  const debug = useDebug()
  const [filter, setFilter] = useState('')
  const rows = useMemo(() => {
    const list: { id: string; label: string }[] = []
    walkObjects(project.objects, (node, ancestors) => {
      if (node.kind !== 'value') return
      list.push({ id: node.id, label: [...ancestors.map((folder) => folder.name || folder.id), node.name || node.id].join('.') })
    })
    return list
  }, [project.objects])
  const shown = useMemo(() => {
    const needle = filter.trim().toLowerCase()
    return rows.filter((row) => !needle || row.label.toLowerCase().includes(needle)).slice(0, MAX_ROWS)
  }, [rows, filter])

  const mode = debug.mode
  const running = debug.active && !debug.stale

  return (
    <div className="program-panel debug-panel">
      <PanelHeader icon={<span className="object-type-icon text"><Bug aria-hidden="true" /></span>} title="Debug" />

      <div className="object-details-section">
        <h3>Session</h3>
        <p className="program-muted">
          {debug.active
            ? debug.stale ? 'The program changed since the debug upload: what is shown may no longer match the canvas.' : (mode ? MODE_TEXT[mode] : 'Running')
            : 'Off. Starting uploads the program with a live view of every block (state strips, tint, values beside the pins).'}
        </p>
        <div className="program-actions">
          <Button variant={debug.active && !debug.stale ? undefined : 'primary'} disabled={debug.busy} onClick={debug.toggle} title={debug.active && !debug.stale ? 'Stop watching (the program keeps running on the board)' : 'Upload the program to the board with a live view of every block (replaces the program on the board, stops the running one)'}>
            <Bug aria-hidden="true" /><span>{debug.busy ? 'Uploading…' : debug.active ? (debug.stale ? 'Upload again' : 'Stop watching') : 'Start debugging'}</span>
          </Button>
        </div>
        {debug.error && (
          <p className="program-diag is-error" role="alert">
            <AlertCircle aria-hidden="true" />
            <span>{debug.error} <button type="button" className="debug-link" onClick={debug.clearError}>dismiss</button></span>
          </p>
        )}
      </div>

      {debug.active && (
        <div className="object-details-section">
          <h3>Run</h3>
          <div className="debug-run" role="group" aria-label="Run controls">
            <Button disabled={!running || mode === 'running'} onClick={() => debug.run('run')} title="Run continuously (VM_EXEC_NORMAL_MODE)"><Play aria-hidden="true" /><span>Run</span></Button>
            <Button disabled={!running || mode === 'paused'} onClick={() => debug.run('pause')} title="Pause before the next block (VM_EXEC_PAUSE)"><Pause aria-hidden="true" /><span>Pause</span></Button>
            <Button disabled={!running || mode !== 'paused'} onClick={() => debug.run('resume')} title="Continue from the pause (VM_EXEC_RESUME)"><Play aria-hidden="true" /><span>Resume</span></Button>
            <Button disabled={!running} onClick={() => debug.run('step')} title="Run exactly one pass, then wait (VM_EXEC_SCAN_MODE, then VM_EXEC_ONCE)"><StepForward aria-hidden="true" /><span>Step pass</span></Button>
            <Button disabled={!running || mode === 'block'} onClick={() => debug.run('block')} title="Wait before every block (VM_EXEC_BLOCK_MODE)"><SkipForward aria-hidden="true" /><span>Block mode</span></Button>
            <Button disabled={!running || mode !== 'block'} onClick={() => debug.run('next')} title="Run the next block (VM_EXEC_NEXT)"><StepForward aria-hidden="true" /><span>Next block</span></Button>
          </div>
          {mode === 'block' && (
            <p className="program-muted">
              {debug.nextBlock ? <>Next: <strong>{debug.nextBlock}</strong> (marked on the canvas). </> : null}
              The board does not report which block is next, so this is counted from the first block. Values refresh when the pass completes.
            </p>
          )}
          {mode === 'paused' && <p className="program-muted">Paused. Variables you subscribe to now are shown after the next pass.</p>}
        </div>
      )}

      {debug.active && (
        <div className="object-details-section">
          <h3>Watching · {debug.subscribed} / {debug.subscribeLimit}</h3>
          <p className="program-muted">Blocks' pins, ENO and enables are watched automatically. Add any other variable here: it is subscribed on the board at once, without an upload.</p>
          {debug.notice && <p className="program-diag is-warning" role="status"><AlertCircle aria-hidden="true" />{debug.notice}</p>}
          <TextField aria-label="Filter variables" placeholder="Filter variables" value={filter} onChange={(event) => setFilter(event.target.value)} />
          <ul className="debug-watch-list">
            {shown.map((row) => {
              const state = debug.watchState(row.id)
              const text = formatLiveValue(debug.read(row.id), 6)
              return (
                <li key={row.id} className={state === 'off' || state === undefined ? 'is-off' : ''}>
                  <span className="debug-watch-name" title={row.label}>{row.label}</span>
                  <code className={text === undefined ? 'is-none' : ''}>{text ?? '–'}</code>
                  {state === 'auto' && <span className="debug-watch-auto" title="Watched for a block">block</span>}
                  {state === 'user' && <button type="button" className="debug-watch-toggle is-on" aria-label={`Stop watching ${row.label}`} title="Stop watching" onClick={() => debug.unwatch(row.id)}><Eye aria-hidden="true" /></button>}
                  {state === 'off' && <button type="button" className="debug-watch-toggle" aria-label={`Watch ${row.label}`} title="Watch (subscribe on the board)" onClick={() => debug.watch(row.id)}><EyeOff aria-hidden="true" /></button>}
                  {state === undefined && <span className="debug-watch-auto" title="Not in the uploaded program: upload again">–</span>}
                </li>
              )
            })}
            {!shown.length && <li className="is-empty">{rows.length ? 'No variable matches.' : 'The project has no variables.'}</li>}
          </ul>
        </div>
      )}
    </div>
  )
}
