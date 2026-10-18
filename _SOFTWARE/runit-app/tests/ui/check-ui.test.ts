import { describe, expect, it } from 'vitest'
// @ts-expect-error a plain ESM script (tools/check-ui.mjs), no types
import { checkSource, checkUi } from '../../tools/check-ui.mjs'

describe('the screens use the shared UI parts (tools/check-ui.mjs)', () => {
  it('no file outside src/ui/components writes raw markup a component draws', () => {
    expect(checkUi()).toEqual([])
  })

  it('finds raw inputs, selects and component classes, and lets a ui-raw line through', () => {
    const source = [
      '<input value={x} />',
      '<select />',
      '<span className="badge is-count">3</span>',
      '<button className={`text-button ${tone}`}>Go</button>',
      '// ui-raw: the canvas draws its own handle',
      '<input type="range" />',
      '<div className="connection-panel" />',
    ].join('\n')
    expect(checkSource(source).map((finding: { line: number }) => finding.line)).toEqual([1, 2, 3, 4])
  })
})
