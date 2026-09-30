import { describe, expect, it } from 'vitest'
import { parseProject, serializeProject, createProject } from '../../../src/domain/project'
import type { ProjectCanvas } from '../../../src/domain/project'
import { addBlock, addCanvas, canvasToScreen, findBlock, GRID, moveBlock, moveCanvas, newBlockId, newCanvasName, pasteBlock, pasteBlocks, programBlocks, removeBlock, removeCanvas, renameCanvas, screenToCanvas, setCanvasDisabled, snap, snapPoint, updateBlock, ZOOM_MAX, zoomAt } from '../../../src/domain/canvas'

const canvas = (id: string, name: string, extra: Partial<ProjectCanvas> = {}): ProjectCanvas => ({ id, name, blocks: [], ...extra })

describe('grid and viewport', () => {
  it('snaps to the nearest grid line', () => {
    expect(GRID).toBe(20)
    expect([snap(9), snap(10), snap(29), snap(-11)]).toEqual([0, 20, 20, -20])
    expect(snapPoint({ x: 33, y: 47 })).toEqual({ x: 40, y: 40 })
  })

  it('maps between screen and canvas both ways', () => {
    const viewport = { x: 100, y: 50, zoom: 2 }
    expect(screenToCanvas(viewport, { x: 140, y: 90 })).toEqual({ x: 20, y: 20 })
    expect(canvasToScreen(viewport, { x: 20, y: 20 })).toEqual({ x: 140, y: 90 })
  })

  it('zooms around the cursor and clamps the zoom', () => {
    const viewport = { x: 0, y: 0, zoom: 1 }
    const zoomed = zoomAt(viewport, 2, { x: 200, y: 100 })
    expect(screenToCanvas(zoomed, { x: 200, y: 100 })).toEqual({ x: 200, y: 100 })
    expect(zoomAt(viewport, 100, { x: 0, y: 0 }).zoom).toBe(ZOOM_MAX)
  })
})

describe('canvas list', () => {
  it('names new canvases with the lowest free number', () => {
    expect(newCanvasName([])).toBe('Canvas 1')
    expect(newCanvasName([canvas('a', 'Canvas 1'), canvas('b', 'Canvas 3')])).toBe('Canvas 2')
    expect(addCanvas([canvas('a', 'Canvas 1')], 'b')).toEqual([canvas('a', 'Canvas 1'), canvas('b', 'Canvas 2')])
    expect(() => addCanvas([canvas('a', 'x')], 'a')).toThrow('taken')
  })

  it('renames, removes and reorders', () => {
    const list = [canvas('a', 'A'), canvas('b', 'B'), canvas('c', 'C')]
    expect(renameCanvas(list, 'b', '  Motors ')[1]?.name).toBe('Motors')
    expect(() => renameCanvas(list, 'b', ' ')).toThrow('name')
    expect(removeCanvas(list, 'b').map((entry) => entry.id)).toEqual(['a', 'c'])
    expect(moveCanvas(list, 'c', 0).map((entry) => entry.id)).toEqual(['c', 'a', 'b'])
    expect(moveCanvas(list, 'a', 9).map((entry) => entry.id)).toEqual(['b', 'c', 'a'])
  })

  it('builds the program from the enabled canvases in order', () => {
    const block = (id: string) => ({ id, type: 'PERIODIC', x: 0, y: 0, settings: { period: 1 } })
    const list = setCanvasDisabled([canvas('a', 'A', { blocks: [block('a1')] }), canvas('b', 'B', { blocks: [block('b1')] }), canvas('c', 'C', { blocks: [block('c1'), block('c2')] })], 'b', true)
    expect(list[1]?.disabled).toBe(true)
    expect(programBlocks(list)).toEqual([
      { id: 'a1', type: 'PERIODIC', settings: { period: 1 } },
      { id: 'c1', type: 'PERIODIC', settings: { period: 1 } },
      { id: 'c2', type: 'PERIODIC', settings: { period: 1 } },
    ])
    expect(setCanvasDisabled(list, 'b', false)[1]).not.toHaveProperty('disabled')
  })

  it('saves and opens canvases with the project', () => {
    const canvases: ProjectCanvas[] = [
      canvas('a', 'Main', {
        blocks: [{ id: 'sum', type: 'EXPR', x: 40, y: 60, inputs: [{ root: 'table', steps: [{ kind: 'dynamic', index: { root: 'sel' } }] }, null], outputs: [null], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] }, eno: true }],
      }),
      canvas('b', 'Spare', { disabled: true }),
    ]
    const project = { ...createProject('p'), canvases }
    expect(parseProject(serializeProject(project)).canvases).toEqual(canvases)
  })

  it('saves a fixed block view with the project and keeps it out of the program', () => {
    const canvases: ProjectCanvas[] = [canvas('a', 'Main', { blocks: [{ id: 'p', type: 'PERIODIC', x: 20, y: 40, view: 'detailed' }] })]
    const restored = parseProject(serializeProject({ ...createProject('p'), canvases })).canvases!
    expect(restored).toEqual(canvases)
    expect(programBlocks(restored)).toEqual([{ id: 'p', type: 'PERIODIC' }])
  })

  it('refuses a block ID used on two canvases', () => {
    const block = { id: 'x', type: 'IF', x: 0, y: 0 }
    const text = serializeProject({ ...createProject('p'), canvases: [canvas('a', 'A')] }).replace('"blocks": []', `"blocks": [${JSON.stringify(block)}]`)
    const twice = JSON.parse(text)
    twice.canvases.push({ id: 'b', name: 'B', blocks: [block] })
    expect(() => parseProject(JSON.stringify(twice))).toThrow("block 'x' is used twice")
  })
})

describe('blocks on canvases', () => {
  const place = (id: string, x = 0, y = 0) => ({ id, type: 'PERIODIC', x, y })
  it('numbers new block IDs per type across every canvas', () => {
    const list = [canvas('a', 'A', { blocks: [place('periodic1')] }), canvas('b', 'B', { blocks: [place('periodic2')] })]
    expect(newBlockId(list, 'PERIODIC')).toBe('periodic3')
    expect(newBlockId(list, 'EXPR_BIT')).toBe('expr_bit1')
  })

  it('adds, moves, updates and removes a block wherever it is', () => {
    let list: ProjectCanvas[] = [canvas('a', 'A'), canvas('b', 'B')]
    list = addBlock(list, 'b', place('p1', 20, 40))
    expect(() => addBlock(list, 'a', place('p1'))).toThrow('taken')
    list = moveBlock(list, 'p1', { x: 60, y: 80 })
    list = updateBlock(list, 'p1', (block) => ({ ...block, id: 'renamed', settings: { period: 5 } }))
    expect(findBlock(list, 'p1')).toEqual({ canvas: list[1], block: { id: 'p1', type: 'PERIODIC', x: 60, y: 80, settings: { period: 5 } } })
    expect(removeBlock(list, 'p1')[1]?.blocks).toEqual([])
    expect(() => removeBlock(list, 'nope')).toThrow('No block')
  })
})

describe('copy and paste', () => {
  it('pastes a copy under a new ID that reads what the original reads but writes no variable', () => {
    const original = { id: 'expr1', type: 'EXPR', x: 40, y: 40, inputs: [{ root: 'if1:q0' }, { root: 'count' }], outputs: ['total'], enables: [{ root: 'if1:q0' }], expression: { code: ['in', 0] } }
    const canvases: ProjectCanvas[] = [canvas('a', 'Main', { blocks: [original] })]
    const { canvases: next, id } = pasteBlock(canvases, 'a', original, { x: 80, y: 80 })
    expect(id).toBe('expr2')
    expect(next[0]!.blocks[1]).toEqual({ ...original, id: 'expr2', x: 80, y: 80, outputs: [null] })
    expect(next[0]!.blocks[0]).toBe(original)
  })
})

describe('copy and paste of several blocks', () => {
  it('keeps the wiring inside the copied group and leaves the wiring to blocks outside it as it was', () => {
    const tick = { id: 'periodic1', type: 'PERIODIC', x: 0, y: 0 }
    const sum = { id: 'expr1', type: 'EXPR', x: 200, y: 0, inputs: [{ root: 'periodic1:eno' }, { root: 'count' }], enables: [{ root: 'periodic1:eno' }, { root: 'other1:q0' }], expression: { code: ['in', 0] } }
    const canvases: ProjectCanvas[] = [canvas('a', 'Main', { blocks: [tick, sum] })]
    const { canvases: next, ids } = pasteBlocks(canvases, 'a', [tick, sum], { x: 40, y: 40 })
    expect(ids).toEqual(['periodic2', 'expr2'])
    const copy = findBlock(next, 'expr2')!.block
    // Read what the copied tick writes, not the original tick.
    expect(copy.inputs).toEqual([{ root: 'periodic2:eno' }, { root: 'count' }])
    expect(copy.enables).toEqual([{ root: 'periodic2:eno' }, { root: 'other1:q0' }])
    expect([copy.x, copy.y]).toEqual([240, 40])
    // The originals are untouched.
    expect(findBlock(next, 'expr1')!.block).toBe(sum)
  })

  it('pastes a single block as pasteBlock does', () => {
    const one = { id: 'expr1', type: 'EXPR', x: 40, y: 40, inputs: [{ root: 'if1:q0' }] }
    const { canvases: next, ids } = pasteBlocks([canvas('a', 'Main', { blocks: [one] })], 'a', [one], { x: 40, y: 40 })
    expect(ids).toEqual(['expr2'])
    expect(findBlock(next, 'expr2')!.block).toEqual({ ...one, id: 'expr2', x: 80, y: 80 })
  })
})
