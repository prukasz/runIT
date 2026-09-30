import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { InlineRename } from '../../../src/components/InlineRename'
import { TreeSlab } from '../../../src/components/TreeSlab/TreeSlab'

/* Inline rename in a tree row: the editor replaces the label, and never sits inside the row's button. */

describe('TreeSlab label editor', () => {
  it('is a button with the label when not editing', () => {
    const html = renderToString(<TreeSlab label="count" />)
    expect(html).toContain('<button')
    expect(html).toContain('>count<')
    expect(html).not.toContain('is-editing')
  })

  it('swaps the button for a plain box holding the editor, so no input is inside a button', () => {
    const editor = <InlineRename className="tree-slab-rename" value="count" label="Name of count" maxLength={15} onCommit={() => undefined} onCancel={() => undefined} />
    const html = renderToString(<TreeSlab label="count" labelEditor={editor} />)
    expect(html).toContain('tree-slab-item is-editing')
    expect(html).toContain('<input')
    expect(html).toContain('maxLength="15"')
    expect(html).not.toContain('<button')
    // The editor takes the label's place: the label span holds the input, not the plain text.
    expect(html).toMatch(/<span class="tree-slab-label"[^>]*><input/)
  })
})
