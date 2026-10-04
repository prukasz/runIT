import { FlipHorizontal2, Plus, RotateCw, Save, Trash2, Undo2 } from 'lucide-react'
import { Button } from '../components/Button'
import { ListableField } from '../components/ListableField'
import { SNAP_STEPS } from './useBoardEditor'
import type { BoardEditorApi, BoardSnap } from './useBoardEditor'
import './Devices.css'

const SNAP_LABELS: Record<BoardSnap, string> = { off: 'Snap off', '0.25': 'Grid 0.25 mm', '0.5': 'Grid 0.5 mm', '1': 'Grid 1 mm', '2.54': 'Grid 2.54 mm' }
const SNAP_OPTIONS = SNAP_STEPS.map((step) => ({ value: step, label: SNAP_LABELS[step] }))

/*
 * The board creator's controls in the app's top toolbar: a new board or one from the library, Save (the project's board
 * and the library), the selected connector's rotate / mirror / delete, the snap, and a way back to the built-in board.
 */
export function BoardEditorToolbar({ editor: e, hasCustomBoard }: { editor: BoardEditorApi; hasCustomBoard: boolean }) {
  const selected = e.selected
  const errors = e.issues.filter((issue) => issue.severity === 'error')
  const saveBlocker = !e.draft ? 'Create a board first' : errors.length ? errors[0].message : !e.dirty ? 'Nothing changed' : undefined
  const libraryOptions = [{ value: '', label: 'Open from library…' }, ...e.library.map((entry) => ({ value: entry.file, label: entry.template.name }))]

  return (
    <div className="board-editor-toolbar" role="group" aria-label="Board creator">
      <Button size="sm" title="Start a new board" onClick={() => e.createBoard()}><Plus aria-hidden="true" />New board</Button>
      {e.library.length > 0 && <ListableField className="board-editor-library-select" aria-label="Open from library" title="Open a board of the library" value="" options={libraryOptions} onChange={(file) => file && e.openFromLibrary(file)} />}
      <Button variant="primary" size="sm" disabled={!!saveBlocker} title={saveBlocker ?? 'Use this board in the project and save it in the library'} onClick={() => void e.save()}><Save aria-hidden="true" />Save</Button>

      <span className="board-editor-sep" />
      <div className="history-actions board-tools" role="group" aria-label="Selected connector">
        <button type="button" aria-label="Rotate (R)" title={selected ? 'Rotate the connector 90° (R)' : 'Select a connector to rotate it'} disabled={!selected} onClick={() => selected && e.rotate(selected.id)}><RotateCw aria-hidden="true" /></button>
        <button type="button" aria-label="Mirror (M)" title={selected ? 'Mirror the connector (M)' : 'Select a connector to mirror it'} disabled={!selected} onClick={() => selected && e.mirror(selected.id)}><FlipHorizontal2 aria-hidden="true" /></button>
        <button type="button" aria-label="Delete connector" title={selected ? 'Remove the connector (Delete)' : 'Select a connector to remove it'} disabled={!selected} onClick={() => selected && e.removeConnector(selected.id)}><Trash2 aria-hidden="true" /></button>
      </div>
      <ListableField className="board-editor-snap" aria-label="Snap" title="Snap while moving a connector" value={e.snap} options={SNAP_OPTIONS} onChange={(value) => e.setSnap(value as BoardSnap)} />

      {hasCustomBoard && (
        <>
          <span className="board-editor-sep" />
          <Button size="sm" title="Go back to the built-in runIT board for this project" onClick={e.useBuiltin}><Undo2 aria-hidden="true" />Use runIT board</Button>
        </>
      )}
    </div>
  )
}
