import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { CanvasBlock, ProjectCanvas } from '../../../src/domain/project'
import type { BlockLive } from '../../../src/domain/upload'
import { DebugProvider, INACTIVE_DEBUG } from '../../../src/debug/DebugContext'
import type { DebugView } from '../../../src/debug/DebugContext'
import { BlockDetails } from '../../../src/canvas/blocks/BlockDetails'
import { blockDiagnostics } from '../../../src/canvas/blocks/blockView'
import { CanvasEditor } from '../../../src/canvas/workspace/CanvasEditor'
import { useCanvasWorkspace } from '../../../src/canvas/workspace/useCanvasWorkspace'
import type { CanvasWorkspace } from '../../../src/canvas/workspace/useCanvasWorkspace'

/* Debug mode drawn: the EN / ENO strips, the block tint and the values beside pins, from a made-up board state. */

const catalog = runitVmCatalog()

const blocks: CanvasBlock[] = [
  { id: 'working', type: 'EXPR', x: 0, y: 0, inputs: [{ root: 'n' }], expression: { code: ['in', 0] } },
  { id: 'closed', type: 'IF', x: 400, y: 0, inputs: [null], enables: [{ root: 'gate' }] },
  { id: 'quiet', type: 'TIMER', x: 0, y: 200 },
  { id: 'broken', type: 'PERIODIC', x: 400, y: 200 },
]

const states: Record<string, BlockLive> = {
  working: { en: 'always', eno: true, enables: [], inputs: ['12.5'], outputs: ['12.5'] },
  closed: { en: 'closed', eno: false, enables: ['0'], inputs: [undefined], outputs: [undefined, undefined] },
  quiet: { en: 'open', eno: false, enables: [], inputs: [], outputs: ['0'] },
  broken: { en: 'open', eno: false, enables: [], inputs: [], outputs: [undefined] },
}

const debug: DebugView = {
  ...INACTIVE_DEBUG,
  active: true,
  block: (id) => states[id],
  failed: (id) => id === 'broken',
}

function Harness({ part = 'editor', selected, view = debug }: { part?: 'editor' | 'details'; selected?: CanvasBlock; view?: DebugView }) {
  const canvases: readonly ProjectCanvas[] = [{ id: 'a', name: 'Main', blocks }]
  const base = useCanvasWorkspace()
  const w: CanvasWorkspace = { ...base, canvases, active: canvases[0], selectedBlock: selected }
  const diagnostics = blockDiagnostics(createProject('t'), canvases, catalog, 240)
  return <DebugProvider value={view}>{part === 'details' ? <BlockDetails workspace={w} diagnostics={diagnostics} /> : <CanvasEditor workspace={w} showGrid diagnostics={diagnostics} />}</DebugProvider>
}

const blockHtml = (html: string, id: string): string => {
  const label = html.indexOf(`aria-label="${catalog.block(blocks.find((block) => block.id === id)!.type)!.title} block ${id}"`)
  const start = html.lastIndexOf('<div', label)
  const next = html.indexOf('<div class="canvas-block ', label)
  return html.slice(start, next < 0 ? undefined : next)
}

describe('debug mode on the canvas', () => {
  const html = renderToString(<Harness />)

  it('tints a block that acted green, with a green ENO strip and the EN strip faded (no gate)', () => {
    expect(html).toMatch(/canvas-block cat-data[^"]* is-debugging dbg-working has-eno/)
    const working = blockHtml(html, 'working')
    expect(working).toContain('canvas-block-enable is-en is-always')
    expect(working).toContain('canvas-block-enable is-eno canvas-wire-start is-active')
  })

  it('greys a block whose gate is closed and reddens its EN strip', () => {
    expect(html).toMatch(/dbg-off eno-false/)
    expect(blockHtml(html, 'closed')).toContain('canvas-block-enable is-en is-closed')
  })

  it('shows enabled-but-idle in amber, and a failed block red', () => {
    expect(html).toContain('dbg-quiet')
    expect(html).toContain('is-open')
    expect(blockHtml(html, 'broken')).toContain('is-failed')
    expect(blockHtml(html, 'broken')).not.toContain('dbg-quiet')
  })

  it('writes the value inside the variable chip of an input and beside a pin without a chip', () => {
    const working = blockHtml(html, 'working')
    expect(working).toMatch(/canvas-chip is-docked is-in[^>]*>.*?<span class="canvas-live">12\.5<\/span>/)
    expect(blockHtml(html, 'quiet')).toMatch(/<span class="canvas-pin-live is-out" title="[^"]*">0<\/span>/)
  })

  it('draws nothing of the kind without a debug session', () => {
    const plain = renderToString(<Harness view={INACTIVE_DEBUG} />)
    for (const marker of ['is-debugging', 'dbg-', 'canvas-pin-live', 'canvas-live', 'is-open', 'is-closed']) expect(plain).not.toContain(marker)
  })

  it('lists EN, ENO and every pin value in the block details', () => {
    const details = renderToString(<Harness part="details" selected={blocks[1]} />)
    expect(details).toContain('aria-label="Live state"')
    expect(details).toContain('closed, not running')
    expect(details).toContain('false, it did not act')
    expect(renderToString(<Harness part="details" selected={blocks[0]} />)).toContain('true, the block acted')
    expect(renderToString(<Harness part="details" selected={blocks[2]} view={{ ...debug, block: () => undefined }} />)).toContain('Waiting for the board')
  })
})
