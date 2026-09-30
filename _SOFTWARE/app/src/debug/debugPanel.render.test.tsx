import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { createProject } from '../domain/project'
import type { ObjectNode, ProjectDocument } from '../domain/project'
import { LiveValueField } from '../ObjectTreeWorkspace'
import { DebugProvider, INACTIVE_DEBUG } from './DebugContext'
import type { DebugView, WatchState } from './DebugContext'
import { DebugPanel } from './DebugPanel'

/* The Debug tab and the watch buttons, drawn from made-up session states. */

const value = (id: string, name: string): ObjectNode => ({ kind: 'value', id, name, type: 'F', length: 1, mutable: true, retentive: false })
const project: ProjectDocument = {
  ...createProject('t'),
  objects: [value('rate', 'Rate'), value('period', 'Period'), value('gone', 'New'), { kind: 'folder', id: 'motor', name: 'motor', children: [value('speed', 'speed')] }],
}
const states: Record<string, WatchState | undefined> = { rate: 'auto', period: 'off', speed: 'user', gone: undefined }
const view = (extra: Partial<DebugView> = {}): DebugView => ({
  ...INACTIVE_DEBUG,
  active: true,
  mode: 'running',
  subscribed: 7,
  subscribeLimit: 128,
  watchState: (id) => states[id],
  read: (id) => (id === 'rate' ? 12.5 : undefined),
  ...extra,
})
const render = (v: DebugView) => renderToString(<DebugProvider value={v}><DebugPanel project={project} /></DebugProvider>).replace(/<!-- -->/g, '')

describe('the Debug tab', () => {
  it('offers to start when off, and has no run controls or watch list', () => {
    const html = render(INACTIVE_DEBUG)
    expect(html).toContain('Start debugging')
    expect(html).not.toContain('Run controls')
    expect(html).not.toContain('Watching')
  })

  it('runs: pause and steps are available, Run and Resume are not', () => {
    const html = render(view())
    expect(html).toContain('Stop watching')
    expect(html).toContain('aria-label="Run controls"')
    const button = (name: string) => html.match(new RegExp(`<button[^>]*>(?:(?!</button>).)*<span>${name}</span></button>`))![0]
    expect(button('Run')).toContain('disabled')
    expect(button('Resume')).toContain('disabled')
    expect(button('Pause')).not.toContain('disabled')
    expect(button('Step pass')).not.toContain('disabled')
    expect(button('Next block')).toContain('disabled')
  })

  it('in block mode names the next block and enables Next block', () => {
    const html = render(view({ mode: 'block', nextBlock: 'every' }))
    expect(html).toContain('Next: <strong>every</strong>')
    expect(html).toContain('does not report which block is next')
    expect(html.match(/<button[^>]*>(?:(?!<\/button>).)*<span>Next block<\/span><\/button>/)![0]).not.toContain('disabled')
  })

  it('lists variables with how each is watched, and the subscription count', () => {
    const html = render(view())
    expect(html).toContain('Watching · 7 / 128')
    expect(html).toContain('>12.5</code>')
    expect(html).toContain('title="Watched for a block">block<')
    expect(html).toContain('aria-label="Watch Period"')
    expect(html).toContain('aria-label="Stop watching motor.speed"')
    expect(html).toContain('title="Not in the uploaded program: upload again"')
  })

  it('says why a chosen variable was not subscribed, and shows a stale session', () => {
    expect(render(view({ notice: '1 chosen variable(s) do not fit' }))).toContain('do not fit')
    expect(render(view({ stale: true }))).toContain('Upload again')
  })
})

describe('watch buttons on a variable row', () => {
  const row = (id: string) => renderToString(<DebugProvider value={view()}><LiveValueField id={id} /></DebugProvider>)
  it('offers Watch for a variable that is not subscribed, Unwatch for one the user chose, nothing for a block\'s', () => {
    expect(row('period')).toContain('>Watch</button>')
    expect(row('speed')).toContain('>Unwatch</button>')
    expect(row('rate')).not.toContain('button')
    expect(row('gone')).not.toContain('button')
  })
})
