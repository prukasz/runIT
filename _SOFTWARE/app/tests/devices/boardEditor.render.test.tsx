import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { newBoard } from '../../src/domain/board'
import type { BoardTemplate } from '../../src/domain/board'
import { BoardEditorDetails } from '../../src/devices/BoardEditorDetails'
import { BoardEditorStage } from '../../src/devices/BoardEditorStage'
import { BoardEditorToolbar } from '../../src/devices/BoardEditorToolbar'
import { BoardPalette } from '../../src/devices/BoardPalette'
import { useBoardEditor } from '../../src/devices/useBoardEditor'
import { useDevicesWorkspace } from '../../src/devices/useDevicesWorkspace'
import type { DevicesWorkspace } from '../../src/devices/useDevicesWorkspace'

const board: BoardTemplate = {
  ...newBoard('lab', 'Lab board'),
  connectors: [
    { id: 'c1', type: 'screw2', x: 16, y: 10, rotation: 0, mirror: false, label: 'Power', pins: [{ label: '3V3', bind: { type: 'net', net: '3V3' } }, { label: 'GND', bind: { type: 'net', net: 'GND' } }] },
    { id: 'c2', type: 'header1x4', x: 20, y: 12, rotation: 0, mirror: false, pins: [{ bind: { type: 'none' } }, { bind: { type: 'none' } }, { bind: { type: 'none' } }, { bind: { type: 'none' } }] },
  ],
}

type Part = 'toolbar' | 'palette' | 'stage' | 'details'

function Harness({ part, withBoard }: { part: Part; withBoard?: boolean }) {
  const base = useDevicesWorkspace()
  const w: DevicesWorkspace = { ...base, board: withBoard ? board : undefined }
  const editor = useBoardEditor(w, false)
  if (part === 'toolbar') return <BoardEditorToolbar editor={editor} hasCustomBoard={!!withBoard} />
  if (part === 'palette') return <BoardPalette editor={editor} onClose={() => undefined} />
  if (part === 'stage') return <BoardEditorStage editor={editor} catalog={w.catalog} />
  return <BoardEditorDetails editor={editor} />
}

const render = (part: Part, withBoard?: boolean) => renderToString(<Harness part={part} withBoard={withBoard} />).replace(/<!-- -->/g, '')

describe('board creator without a board', () => {
  it('the stage offers to create one, and says what a board is', () => {
    const html = render('stage')
    expect(html).toContain('Create a board')
    expect(html).toContain('screw terminals, pin headers, XT60, USB-C')
  })

  it('the toolbar can start a board but not save', () => {
    const html = render('toolbar')
    expect(html).toContain('New board')
    expect(html).toContain('Create a board first')
    expect(html).not.toContain('Use runIT board')
  })

  it('the palette lists every premade connector, group by group, disabled until there is a board', () => {
    const html = render('palette')
    for (const text of ['Screw terminals', 'Pin headers', 'Power', 'Screw terminal 2-pin', 'Pin header 2x4', 'XT60 power', 'USB-C power', 'Back to Board canvas']) expect(html).toContain(text)
    expect(html).toContain('is-disabled')
  })

  it('the panel asks for a board first', () => {
    expect(render('details')).toContain('Create a board to start')
  })
})

describe('board creator on a board', () => {
  it('the stage draws the board art with a terminal per connector pin', () => {
    const html = render('stage', true)
    expect(html).toContain('board-editor-stage')
    expect(html).toContain('<g id="c1-1">')
    expect(html).toContain('<g id="c2-4">')
    expect(html).toContain('Lab board · 80 × 60 mm · 2 connectors')
  })

  it('the panel edits the board: name, ID, size, outline, holes', () => {
    const html = render('details', true)
    for (const text of ['Name', 'ID', 'Width (mm)', 'Height (mm)', 'Corner radius', 'Hole diameter', 'Corner mount holes']) expect(html).toContain(text)
    expect(html).toContain('value="Lab board"')
    expect(html).toContain('Select a connector on the board to edit its pins.')
  })

  it('shows the checks: the two connectors overlap', () => {
    expect(render('details', true)).toContain('overlap')
  })

  it('the toolbar can use the built-in board again, and rotate / mirror need a selected connector', () => {
    const html = render('toolbar', true)
    expect(html).toContain('Use runIT board')
    expect(html).toContain('Select a connector to rotate it')
    expect(html).toContain('Select a connector to mirror it')
  })
})
