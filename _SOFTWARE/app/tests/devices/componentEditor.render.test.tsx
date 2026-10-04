import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../../src/domain/descriptors'
import { ComponentStage } from '../../src/devices/ComponentEditor'
import { ComponentPins } from '../../src/devices/ComponentPins'
import { ComponentToolbar } from '../../src/devices/ComponentToolbar'
import { useComponentEditor } from '../../src/devices/useComponentEditor'

const catalog = runitDeviceCatalog()

function Harness({ part }: { part: 'toolbar' | 'stage' | 'pins' }) {
  const editor = useComponentEditor(catalog, false)
  if (part === 'toolbar') return <ComponentToolbar editor={editor} />
  if (part === 'pins') return <ComponentPins editor={editor} />
  return <ComponentStage editor={editor} />
}

const render = (part: 'toolbar' | 'stage' | 'pins') => renderToString(<Harness part={part} />).replace(/<!-- -->/g, '')

describe('Anchor editor renders', () => {
  it('toolbar: device, new variant, picture and a size slider', () => {
    const html = render('toolbar')
    expect(html).toContain('PCA9685 PWM expander')
    expect(html).toContain('New variant')
    expect(html).toContain('Choose picture')
    expect(html).toContain('aria-label="Width in millimetres"')
    expect(html).toContain('step="5"')
    expect(html).toContain('Save')
  })

  it('right panel: every pin of the device, none placed yet', () => {
    const html = render('pins')
    expect(html).toContain('0 / 28 placed')
    expect(html.match(/component-pin-dot kind-/g)).toHaveLength(28)
  })

  it('stage: asks for a picture first', () => {
    expect(render('stage')).toContain('Choose a picture')
  })
})
