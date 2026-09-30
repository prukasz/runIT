import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { DebugProvider, INACTIVE_DEBUG } from './debug/DebugContext'
import type { DebugView } from './debug/DebugContext'
import { LiveValueField } from './ObjectTreeWorkspace'

/* A variable's row in the Code view shows what the board holds while debug mode runs. */

const debug = (values: Record<string, number | boolean | readonly number[]>): DebugView => ({ ...INACTIVE_DEBUG, active: true, read: (id) => values[id] })
const render = (view: DebugView, id: string) => renderToString(<DebugProvider value={view}><LiveValueField id={id} /></DebugProvider>)

describe('live value on a variable row', () => {
  it('is not there without a debug session', () => {
    expect(render(INACTIVE_DEBUG, 'period')).toBe('')
  })

  it('shows the value the board holds', () => {
    const html = render(debug({ period: 12.3456 }), 'period')
    expect(html).toContain('<span>Live</span>')
    expect(html).toContain('>12.35</output>')
  })

  it('shows arrays short, and a dash for a variable no block reads or writes', () => {
    expect(render(debug({ table: [1, 2, 3, 4, 5] }), 'table')).toContain('[1 2 3 4 5]')
    const none = render(debug({}), 'unused')
    expect(none).toContain('is-none')
    expect(none).toContain('>–</output>')
  })
})
