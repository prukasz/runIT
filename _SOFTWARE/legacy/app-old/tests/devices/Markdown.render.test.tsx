import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { Markdown } from '../../src/devices/Markdown'

describe('device guide Markdown', () => {
  it('renders GitHub-flavoured Markdown without raw HTML', () => {
    const html = renderToString(<Markdown source={[
      '# Title', '', 'Some *italic*, **bold**, ~~old~~ and `code`.', '',
      '> A note', '', '- [x] done', '- [ ] open', '',
      '| Pin | Use |', '|---|---|', '| 0 | OE |', '',
      '1. first', '   - nested', '',
      '<script>alert(1)</script>', '', '[site](https://example.com) [here](#title)',
    ].join('\n')} />)
    expect(html).toContain('<h1>Title</h1>')
    expect(html).toContain('<em>italic</em>')
    expect(html).toContain('<del>old</del>')
    expect(html).toContain('<blockquote>')
    expect(html).toMatch(/<input type="checkbox" disabled="" checked=""\/>/)
    expect(html).toContain('<div class="device-markdown-table"><table>')
    expect(html).toMatch(/<ol>\s*<li>first(<!-- -->)?\s*<ul>\s*<li>nested<\/li>/)
    expect(html).not.toContain('<script>')
    expect(html).toContain('<a href="https://example.com" target="_blank" rel="noreferrer">site</a>')
    expect(html).toContain('<a href="#title">here</a>')
  })

  it('resolves images next to the guides, keeps web images, marks missing ones', () => {
    const html = renderToString(<Markdown source={'![Channels](images/device_pca9685/channels.svg "On the board")\n\n![Web](https://example.com/a.png)\n\n![Gone](images/none.png)'} />)
    // Vite bundles the file (small ones inline as a data URL).
    expect(html).toMatch(/<img src="(data:image\/svg\+xml|[^"]*channels)[^"]*" alt="Channels" loading="lazy"\/><span class="device-markdown-caption">On the board<\/span>/)
    expect(html).toContain('<img src="https://example.com/a.png" alt="Web"')
    expect(html).toContain('Image not found: images/none.png')
  })
})
