import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { CanvasWires } from '../../../src/canvas/surface/CanvasWiring'
import type { BlockShape } from '../../../src/canvas/blocks/blockView'
import type { CanvasBlock, ProjectCanvas } from '../../../src/domain/project'

const shape: BlockShape = { width: 160, height: 60, inputs: [], outputs: [], row: 24 }
const render = (blocks: CanvasBlock[]) => renderToString(<CanvasWires canvas={{ id: 'c', name: 'Main', blocks } as ProjectCanvas} shapeOf={() => shape} onSelect={() => undefined} />)
const tick: CanvasBlock = { id: 'every', type: 'PERIODIC', x: 0, y: 0 }

describe('EN links on the canvas', () => {
  it('are a ribbon that ends in an arrowhead pointing at the middle of the target', () => {
    const html = render([tick, { id: 'act', type: 'TIMER', x: 300, y: 0, enables: [{ root: 'every:eno' }] }])
    const path = /class="canvas-ribbon" d="([^"]+)"/.exec(html)?.[1]
    // The ribbon narrows to a band that ends 12 px before the target's edge (x 300); a small head (8 px each way) points at its EN connector.
    const tipY = /L 300 (\d+)/.exec(path!)?.[1]
    expect(path).toContain(`L 288 ${Number(tipY) - 8} L 300 ${tipY} L 288 ${Number(tipY) + 8}`)
    expect(Number(tipY)).toBeLessThan(60 / 2 + 1)
    expect(html).not.toContain('canvas-wire-line')
  })

  it('keeps the arrowhead on a link that runs back under the blocks', () => {
    const html = render([{ id: 'act', type: 'TIMER', x: 0, y: 0, enables: [{ root: 'every:eno' }] }, { ...tick, x: 400 }])
    expect(/class="canvas-ribbon" d="([^"]+)"/.exec(html)?.[1]).toMatch(/L 0 \d+ L -12 \d+/)
  })
})
