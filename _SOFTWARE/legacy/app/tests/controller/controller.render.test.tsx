import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { ControllerDetails, ControllerPalette, ControllerStage, useControllerWorkspace } from '../../src/controller'
import type { ControllerWorkspace } from '../../src/controller'
import { WIDGET_KINDS } from '../../src/domain/controller'
import type { Controller, ControllerWidget } from '../../src/domain/controller'
import { createProject } from '../../src/domain/project'
import type { ProjectDocument } from '../../src/domain/project'
import { useObjectTreeWorkspace } from '../../src/ObjectTreeWorkspace'

/* Server-renders the Remote view's three parts, so a widget or panel that throws fails here instead of in the browser. */

const project: ProjectDocument = {
  ...createProject('t'),
  objects: [
    { kind: 'value', id: 'speed', name: 'speed', type: 'F', length: 1, mutable: true, retentive: false },
    { kind: 'value', id: 'limit', name: 'limit', type: 'U8', length: 1, mutable: false, retentive: false },
  ],
}

const everyKind: Controller = {
  widgets: WIDGET_KINDS.map((kind, index): ControllerWidget => ({ id: `${kind}1`, kind, x: (index % 4) * 6, y: Math.floor(index / 4) * 4, w: 5, h: 3, links: kind === 'slider' ? { value: 'speed' } : {}, options: {} })),
}

function Harness({ part, controller = everyKind, selectedId, fullscreen = false }: { part: 'stage' | 'palette' | 'details'; controller?: Controller; selectedId?: string; fullscreen?: boolean }) {
  const base = useControllerWorkspace()
  const objects = useObjectTreeWorkspace()
  const workspace: ControllerWorkspace = { ...base, controller, selected: controller.widgets.find((widget) => widget.id === selectedId), selectedId, fullscreen }
  if (part === 'stage') return <ControllerStage workspace={workspace} project={project} />
  if (part === 'palette') return <ControllerPalette workspace={workspace} objects={objects} palette="Widgets" onPalette={() => undefined} />
  return <ControllerDetails workspace={workspace} project={project} selectedVariableId={undefined} />
}

describe('controller renders', () => {
  it('draws every widget kind on the grid, linked or not', () => {
    const html = renderToString(<Harness part="stage" selectedId="slider1" />)
    for (const kind of WIDGET_KINDS) expect(html).toContain(`kind-${kind}`)
    expect(html).toContain('controller-resize')
    expect(html.match(/controller-resize/g)).toHaveLength(1)
    // An unlinked widget is flagged in the editor.
    expect(html).toContain('Not linked to a variable')
  })

  it('shows an empty controller with a hint, and full screen without the editor chrome', () => {
    expect(renderToString(<Harness part="stage" controller={{ widgets: [] }} />)).toContain('An empty controller')
    const full = renderToString(<Harness part="stage" fullscreen />)
    expect(full).toContain('is-fullscreen')
    expect(full).toContain('Leave full screen')
  })

  it('lists the widgets in groups, with the keyboard one marked for laptops', () => {
    const html = renderToString(<Harness part="palette" />)
    for (const title of ['Joystick', 'Slider', 'Input field', 'Keyboard', 'Chart', 'Lamp']) expect(html).toContain(title)
    expect(html).toContain('laptop')
    expect(html).toContain('Widgets')
    expect(html).toContain('Variables')
  })

  it('offers the selected widget its variables and settings, by name, never by ID', () => {
    const html = renderToString(<Harness part="details" selectedId="slider1" />)
    expect(html).toContain('Variables')
    expect(html).toContain('speed')
    expect(html).toContain('Lowest value')
    // The common settings are open, the rest sit behind Advanced, and position and size are set on the grid, not here.
    expect(html).toContain('Colour')
    expect(html).toContain('controller-advanced')
    expect(html).toContain('>Advanced<')
    // Closed until opened: its settings are not in the page yet, only the heading and how many there are.
    expect(html).not.toContain('On release')
    expect(html.indexOf('Lowest value')).toBeLessThan(html.indexOf('controller-advanced'))
    expect(html).toMatch(/Advanced<\/span><em>4<\/em>/)
    expect(html).not.toContain('Position and size')
    expect(renderToString(<Harness part="details" />)).toContain('Select a widget')
  })
})
