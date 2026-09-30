import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../domain/descriptors'
import { createProject } from '../../domain/project'
import type { CanvasBlock, ProjectCanvas } from '../../domain/project'
import { BlockDetails } from '../blocks/BlockDetails'
import { BlockPalette } from './BlockPalette'
import { CanvasEditor } from './CanvasEditor'
import { blockDiagnostics } from '../blocks/blockView'
import { useCanvasWorkspace } from './useCanvasWorkspace'
import type { CanvasWorkspace } from './useCanvasWorkspace'

/* Server-renders the canvas mode, the block palette and the block details, so a page that throws fails here instead of in the browser. */

const catalog = runitVmCatalog()

function Harness({ canvases, showGrid = true, part = 'editor', selected, detailed = false }: { canvases: readonly ProjectCanvas[]; showGrid?: boolean; part?: 'editor' | 'palette' | 'details'; selected?: CanvasBlock; detailed?: boolean }) {
  const base = useCanvasWorkspace()
  const w: CanvasWorkspace = { ...base, canvases, active: canvases[0], selectedBlock: selected, ...(detailed ? { detailed: true } : {}) }
  const diagnostics = blockDiagnostics(createProject('t'), canvases, catalog, 240)
  if (part === 'palette') return <BlockPalette workspace={w} />
  if (part === 'details') return <BlockDetails workspace={w} diagnostics={diagnostics} />
  return <CanvasEditor workspace={w} showGrid={showGrid} diagnostics={diagnostics} />
}

describe('canvas renders', () => {
  it('the open canvas, its grid and the tabs in execution order', () => {
    const html = renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks: [] }, { id: 'b', name: 'Spare', disabled: true, blocks: [] }]} />)
    expect(html).toContain('radial-gradient')
    expect(html).toContain('Drag blocks here')
    expect(html).toMatch(/canvas-tab selected[^>]*>.*?>1<.*?Main/)
    expect(html).toMatch(/canvas-tab is-disabled[^>]*>.*?>2<.*?Spare/)
    expect(html).toContain('100%')
  })

  it('no canvas: offers a new one, no grid when hidden', () => {
    const html = renderToString(<Harness canvases={[]} showGrid={false} />)
    expect(html).toContain('No canvas yet')
    expect(html).not.toContain('radial-gradient')
  })

  it('blocks with their pins, and the compiler\'s error count', () => {
    const blocks: CanvasBlock[] = [{ id: 'periodic1', type: 'PERIODIC', x: 40, y: 60 }, { id: 'if1', type: 'IF', x: 240, y: 60 }]
    const html = renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} selected={blocks[1]} />)
    expect(html).toContain('left:40px;top:60px;width:200px;height:60px')
    expect(html).toContain('periodic1')
    expect(html).toContain('aria-label="Run when connector periodic1"')
    expect(html).toContain('aria-label="Tick connector periodic1"')
    expect(html).not.toContain('canvas-block-expand')
    expect(html).not.toContain('canvas-block-summary')
    expect(html).toMatch(/canvas-block cat-flow selected has-errors/)
    expect(html).toContain('canvas-block-errors')
  })

  it('the face: a value part drawn apart, the pin number and device of an IO block, the tick named beside its strip', () => {
    const blocks: CanvasBlock[] = [
      { id: 'every', type: 'PERIODIC', x: 0, y: 0, settings: { period: 300, time_base: 'MS' } },
      { id: 'led', type: 'IO_TOGGLE', x: 300, y: 0, settings: { device_id: 1, default_io_num: 22 } },
      { id: 'when', type: 'IF', x: 600, y: 0 },
    ]
    const html = renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} />).replace(/<!-- -->/g, '')
    expect(html).toContain('Every <span class="canvas-block-value">300 MS</span>')
    expect(html).toContain('Toggle Pin <span class="canvas-block-value">#22</span>')
    expect(html).toMatch(/<span class="canvas-block-device[^"]*" title="[^"]*\(#1\)">[^<]*\(#1\)<\/span>/)
    // Only a block whose ENO has a name of its own carries the tag.
    expect(html.match(/canvas-block-eno-tag/g)).toHaveLength(1)
    expect(html).toMatch(/canvas-block-eno-tag" title="Tick: [^"]*">Tick</)
  })

  it('a block whose face says it all has no detailed view, whatever the toolbar or its own setting says', () => {
    const every: CanvasBlock = { id: 'every', type: 'PERIODIC', x: 0, y: 0, view: 'detailed', settings: { period: 100, time_base: 'MS' } }
    const expr: CanvasBlock = { id: 'expr1', type: 'EXPR', x: 300, y: 0, inputs: [null], expression: { constants: [2], code: ['in', 0, 'const', 0, '*'] } }
    const canvases = [{ id: 'a', name: 'Main', blocks: [every, expr] }]
    const html = renderToString(<Harness canvases={canvases} detailed />)
    // Only the expression block expands; the Every block keeps its simple height.
    expect(html.match(/canvas-block-summary/g)).toHaveLength(1)
    expect(html).toContain('left:0;top:0;width:200px;height:60px')
    expect(html).not.toMatch(/canvas-block cat-time[^"]* is-expanded/)
    // Its details offer no choice of view, an ordinary block's do.
    expect(renderToString(<Harness part="details" canvases={canvases} selected={every} />)).not.toContain('Block view')
    expect(renderToString(<Harness part="details" canvases={canvases} selected={expr} />)).toContain('Block view')
  })

  it('a fixed detailed view shows content even with the toolbar preference off', () => {
    const blocks: CanvasBlock[] = [{ id: 'expr1', type: 'EXPR', x: 0, y: 0, view: 'detailed', inputs: [null], expression: { constants: [2], code: ['in', 0, 'const', 0, '*'] } }]
    const html = renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} />)
    expect(html).toContain('canvas-block-summary is-formula')
    expect(html).toContain('IN0 * 2')
    expect(html).toContain('width:280px')
  })

  it('hardware details start with two-way running controls and show dynamic masks only when selected', () => {
    const block: CanvasBlock = { id: 'io', type: 'IO_SET_LEVEL', x: 0, y: 0, settings: { device_id: 0, default_io_num: 4 } }
    const render = (selected: CanvasBlock) => renderToString(<Harness part="details" canvases={[{ id: 'a', name: 'Main', blocks: [selected] }]} selected={selected} />)
    const fixed = render(block)
    expect(fixed.indexOf('<h3>Running</h3>')).toBeLessThan(fixed.indexOf('<h3>Settings</h3>'))
    expect(fixed).not.toContain('role="group" aria-label="Branch"')
    expect(fixed).toContain('Always runs (no Run when set)')
    // Any / all only matters with two gates of its own.
    expect(fixed).not.toContain('aria-label="Conditions combine"')
    expect(render({ ...block, enables: [{ root: 'a' }, { root: 'b' }] })).toContain('role="group" aria-label="Conditions combine"')
    expect(fixed).toContain('role="group" aria-label="On error"')
    expect(fixed).toContain('GPIO_ESP (#0)')
    expect(fixed).not.toContain('<legend>Allowed pins</legend>')
    expect(fixed).not.toContain('Allowed mask')
    expect(fixed).not.toContain('No enables: always enabled')
    expect(fixed).not.toContain('ENO output')
    const dynamic = render({ ...block, dynamicInputs: [1], settings: { ...block.settings, allowed_mask: '0x10' } })
    expect(dynamic).toContain('<legend>Allowed pins</legend>')
    expect(dynamic).toContain('type="checkbox" disabled="" checked=""')
  })

  it('the palette lists every block type by category', () => {
    const html = renderToString(<Harness part="palette" canvases={[{ id: 'a', name: 'Main', blocks: [] }]} />)
    for (const type of catalog.blocks) expect(html).toContain(type.title)
    expect(html).toContain('draggable="true"')
  })

  it('the details of a selected block: settings, enums, expression, problems', () => {
    const timer: CanvasBlock = { id: 'timer1', type: 'TIMER', x: 0, y: 0, settings: { mode: 'TOF', pt: 300 } }
    const timerHtml = renderToString(<Harness part="details" canvases={[{ id: 'a', name: 'Main', blocks: [timer] }]} selected={timer} />)
    expect(timerHtml).toContain('Timer')
    expect(timerHtml).toMatch(/<option value="TOF" selected=""/)
    expect(timerHtml).toContain('value="300"')
    expect(timerHtml).toContain('must be wired')
    const expr: CanvasBlock = { id: 'expr1', type: 'EXPR', x: 0, y: 0, inputs: [null, null], expression: { constants: [2], code: ['in', 0, 'const', 0, '*'] } }
    const exprHtml = renderToString(<Harness part="details" canvases={[{ id: 'a', name: 'Main', blocks: [expr] }]} selected={expr} />)
    // At rest the formula shows what each input is wired to (an unwired one as IN0 ?).
    expect(exprHtml).toContain('>IN0 ?</span>')
    expect(exprHtml).toContain('<span class="expr-tok-op">*</span>')
    expect(exprHtml).not.toContain('Targets')
    expect(exprHtml).not.toContain('<code>')
    expect(exprHtml).not.toContain('Apply')
    expect(exprHtml).toContain('Inputs')
    expect(renderToString(<Harness part="details" canvases={[]} />)).toContain('Select a block')
  })
})
