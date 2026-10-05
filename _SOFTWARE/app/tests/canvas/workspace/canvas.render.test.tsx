import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { CanvasBlock, ProjectCanvas } from '../../../src/domain/project'
import { BlockDetails } from '../../../src/canvas/blocks/BlockDetails'
import { BlockPalette } from '../../../src/canvas/workspace/BlockPalette'
import { CanvasEditor } from '../../../src/canvas/workspace/CanvasEditor'
import { blockDiagnostics } from '../../../src/canvas/blocks/blockView'
import { useCanvasWorkspace } from '../../../src/canvas/workspace/useCanvasWorkspace'
import type { CanvasWorkspace } from '../../../src/canvas/workspace/useCanvasWorkspace'

/* Server-renders the canvas mode, the block palette and the block details, so a page that throws fails here instead of in the browser. */

const catalog = runitVmCatalog()

function Harness({ canvases, showGrid = true, part = 'editor', selected, selection, detailed = false }: { canvases: readonly ProjectCanvas[]; showGrid?: boolean; part?: 'editor' | 'palette' | 'details'; selected?: CanvasBlock; selection?: readonly CanvasBlock[]; detailed?: boolean }) {
  const base = useCanvasWorkspace()
  const w: CanvasWorkspace = { ...base, canvases, active: canvases[0], selectedBlock: selected, selectedBlocks: selection ?? (selected ? [selected] : []), selectedIds: (selection ?? (selected ? [selected] : [])).map((entry) => entry.id), ...(detailed ? { detailed: true } : {}) }
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
    expect(html).toContain('left:40px;top:60px;width:160px;height:60px')
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

  it('a gated block says "Running when" with an OR / AND switch, and nothing below its conditions', () => {
    const gated: CanvasBlock = { id: 'p', type: 'PERIODIC', x: 0, y: 0, enables: [{ root: 'a' }, { root: 'b' }] }
    const text = (block: CanvasBlock) => renderToString(<Harness part="details" canvases={[{ id: 'c', name: 'Main', blocks: [block] }]} selected={block} />).replace(/<!-- -->/g, '')
    const any = text(gated)
    expect(any).toContain('<h3>Running when</h3>')
    expect(any).toMatch(/aria-label="Conditions combine"[^>]*><button[^>]*aria-pressed="true"[^>]*>OR<\/button><button[^>]*aria-pressed="false"[^>]*>AND/)
    expect(text({ ...gated, enableMode: 'all' })).toMatch(/aria-label="Conditions combine"[^>]*><button[^>]*aria-pressed="false"[^>]*>OR<\/button><button[^>]*aria-pressed="true"[^>]*>AND/)
    expect(text({ ...gated, enables: [{ root: 'a' }] })).not.toContain('Conditions combine')
    expect(any).not.toContain('every cycle')
    expect(any).not.toMatch(/>Runs/) // no sentence under the list (the list keeps its aria-label)
    expect(any).not.toContain('Delete block')
    // On error is a setting of the block: in Settings, above the pins.
    expect(any.indexOf('aria-label="On error"')).toBeGreaterThan(any.indexOf('<h3>Settings</h3>'))
    expect(any.indexOf('aria-label="On error"')).toBeLessThan(any.indexOf('<h3>Pins</h3>'))
  })

  it('a block has a detailed view only when it has inputs with a constant to show, or a formula', () => {
    const plain: CanvasBlock = { id: 'if1', type: 'IF', x: 0, y: 0 }
    const every: CanvasBlock = { id: 'every', type: 'PERIODIC', x: 300, y: 0, settings: { period: 100, time_base: 'MS' } }
    const expr: CanvasBlock = { id: 'expr1', type: 'EXPR', x: 600, y: 0, inputs: [null], expression: { constants: [2], code: ['in', 0, 'const', 0, '*'] } }
    const canvases = [{ id: 'a', name: 'Main', blocks: [plain, every, expr] }]
    const html = renderToString(<Harness canvases={canvases} detailed />)
    // Only the expression block shows a formula; there are no text rows for any block.
    expect(html.match(/canvas-block-summary/g)).toHaveLength(1)
    expect(html).not.toMatch(/canvas-block cat-flow[^"]* is-expanded/)
    // The hard-coded input of Every is shown, with its constant, only in the detailed view.
    expect(html.match(/is-constant/g)).toHaveLength(1)
    expect(html).toContain('canvas-chip-text">100<')
    expect(renderToString(<Harness canvases={canvases} />)).not.toContain('is-constant')
    // The view is the toolbar's alone: no block has a view setting of its own.
    for (const selected of [plain, every, expr]) expect(renderToString(<Harness part="details" canvases={canvases} selected={selected} />)).not.toContain('Block view')
  })

  it('the toolbar switches a block between its simple and detailed face', () => {
    const blocks: CanvasBlock[] = [{ id: 'expr1', type: 'EXPR', x: 0, y: 0, inputs: [null], expression: { constants: [2], code: ['in', 0, 'const', 0, '*'] } }]
    const html = renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} detailed />)
    expect(html).toContain('canvas-block-summary is-formula')
    expect(html).toContain('IN0 * 2')
    // A block is as wide as its content needs, not a fixed width: a short formula stays narrow, a longer one is wider, up to a limit.
    const widthOf = (blocks: CanvasBlock[]) => Number(/width:(\d+)px/.exec(renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} detailed />))![1])
    const short = widthOf(blocks)
    expect(short).toBeGreaterThanOrEqual(120)
    expect(short).toBeLessThan(200)
    const long: CanvasBlock[] = [{ ...blocks[0]!, expression: { constants: [1, 2, 3, 4, 5, 6, 7, 8], code: ['in', 0, 'const', 0, '*', 'const', 1, '+', 'const', 2, '+', 'const', 3, '+', 'const', 4, '+', 'const', 5, '+', 'const', 6, '+', 'const', 7, '+'] } }]
    expect(widthOf(long)).toBeGreaterThan(short)
    expect(widthOf(long)).toBeLessThanOrEqual(560)
    // The summary under the pins is one line tall for a formula that fits: no empty second line.
    const heightOf = (blocks: CanvasBlock[]) => Number(/height:(\d+)px/.exec(renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} detailed />))![1])
    expect(heightOf(blocks)).toBe(40 + 20 + 20 + 16)
    expect(renderToString(<Harness canvases={[{ id: 'a', name: 'Main', blocks }]} />)).not.toContain('canvas-block-summary is-formula')
  })

  it('several selected blocks: each drawn selected, and the details summarise them', () => {
    const blocks: CanvasBlock[] = [{ id: 'periodic1', type: 'PERIODIC', x: 0, y: 0 }, { id: 'if1', type: 'IF', x: 300, y: 0 }, { id: 'if2', type: 'IF', x: 600, y: 0 }]
    const canvases = [{ id: 'a', name: 'Main', blocks }]
    const html = renderToString(<Harness canvases={canvases} selection={[blocks[1]!, blocks[2]!]} />)
    expect(html.match(/canvas-block cat-flow selected/g)).toHaveLength(2)
    expect(html).not.toMatch(/canvas-block cat-time[^"]* selected/)
    const details = renderToString(<Harness part="details" canvases={canvases} selection={[blocks[1]!, blocks[2]!]} />).replace(/<!-- -->/g, '')
    expect(details).toContain('2 blocks selected')
    expect(details).toContain('Delete 2 blocks')
    expect(details).not.toContain('Block view')
  })

  it('hardware details end with two-way running controls and show dynamic masks only when selected', () => {
    const block: CanvasBlock = { id: 'io', type: 'IO_SET_LEVEL', x: 0, y: 0, settings: { device_id: 0, default_io_num: 4 } }
    const render = (selected: CanvasBlock) => renderToString(<Harness part="details" canvases={[{ id: 'a', name: 'Main', blocks: [selected] }]} selected={selected} />)
    const fixed = render(block)
    // A block that always runs says just "Runs: Always" (no Running heading, no second line); the On error switch is still there.
    const plain = fixed.replace(/<!-- -->/g, '')
    expect(plain).toContain('<h3>Runs: <span class="block-gates-always">Always</span></h3>')
    expect(plain).not.toContain('<h3>Running')
    expect(plain).not.toContain('Runs: every cycle')
    // With its own gates the heading comes back, below the settings and pins.
    const gated = render({ ...block, enables: [{ root: 'a' }] }).replace(/<!-- -->/g, '')
    expect(gated.indexOf('<h3>Pins</h3>')).toBeLessThan(gated.indexOf('<h3>Running when</h3>'))
    expect(gated.indexOf('<h3>Settings</h3>')).toBeLessThan(gated.indexOf('<h3>Running when</h3>'))
    expect(fixed).not.toContain('role="group" aria-label="Branch"')
    expect(fixed).not.toContain('no Run when set')
    // Any / all only matters with two gates of its own.
    expect(fixed).not.toContain('aria-label="Conditions combine"')
    expect(render({ ...block, enables: [{ root: 'a' }, { root: 'b' }] })).toContain('role="group" aria-label="Conditions combine"')
    expect(fixed).toContain('role="group" aria-label="On error"')
    expect(fixed).toContain('GPIO_ESP (#0)')
    expect(fixed).not.toContain('<legend>Allowed pins</legend>')
    expect(fixed).not.toContain('Allowed mask')
    expect(fixed).not.toContain('No enables: always enabled')
    expect(fixed).not.toContain('ENO output')
    // A new block has no device until one is chosen: an empty selector, no pin choice yet, and the compiler says so.
    const unset = render({ id: 'io', type: 'IO_SET_LEVEL', x: 0, y: 0 })
    expect(unset).toContain('Select a device')
    expect(unset).not.toContain('(unavailable)')
    expect(unset).not.toContain('<span>Pin</span>')
    expect(unset).toContain('choose a device for it')
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
    // The source pickers: Any first and selected, then a device, channel and event the chosen domain offers.
    const event: CanvasBlock = { id: 'event1', type: 'ON_EVENT', x: 0, y: 0, settings: { domain: 'POWER', device_id: 12 } }
    const eventHtml = renderToString(<Harness part="details" canvases={[{ id: 'a', name: 'Main', blocks: [event] }]} selected={event} />)
    expect(eventHtml).toContain('<option value="">Any</option>')
    expect(eventHtml).toMatch(/<option value="12" selected="">INA3221 \(#12\)/)
    expect(eventHtml).toContain('Monitor Channel')
    expect(eventHtml).toContain('Over-Current Critical')
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
