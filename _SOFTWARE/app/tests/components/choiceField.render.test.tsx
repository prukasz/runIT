import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { ChoiceField } from '../../src/components/ChoiceField'

const choices = [{ value: 1, label: 'Motor (#1)' }, { value: 2, label: 'Fan (#2)', description: 'Cooling', group: 'Outputs' }]

describe('ChoiceField', () => {
  it('lists the choices, the chosen one selected, descriptions and groups kept', () => {
    const html = renderToString(<ChoiceField value={2} choices={choices} onChange={() => undefined} />)
    expect(html).toContain('<option value="1">Motor (#1)</option>')
    expect(html).toContain('<optgroup label="Outputs"><option value="2" title="Cooling" selected="">Fan (#2)</option></optgroup>')
  })

  it('asks while nothing is chosen, and keeps a stored value the list no longer offers', () => {
    expect(renderToString(<ChoiceField value={undefined} placeholder="Select a device" choices={choices} onChange={() => undefined} />)).toContain('<option value="" selected="">Select a device</option>')
    expect(renderToString(<ChoiceField value={7} choices={choices} unavailableLabel={(id) => `Device ${id} (unavailable)`} onChange={() => undefined} />)).toContain('<option value="7" selected="">Device 7 (unavailable)</option>')
  })

  it('offers an empty option that can be chosen again when asked for', () => {
    expect(renderToString(<ChoiceField value={1} emptyLabel="Any" choices={choices} onChange={() => undefined} />)).toContain('<option value="">Any</option>')
    expect(renderToString(<ChoiceField value={1} choices={choices} onChange={() => undefined} />)).not.toContain('value=""')
  })
})

describe('ChoiceField sizes', () => {
  it('takes size overrides as the field variables and a fixed width', () => {
    const html = renderToString(<ChoiceField value={1} choices={choices} width={120} height={26} fontSize={11} fontWeight={400} onChange={() => undefined} />)
    expect(html).toContain('width:120px')
    expect(html).toContain('--field-height:26px')
    expect(html).toContain('--field-font-size:11px')
    expect(html).toContain('--listable-font-weight:400')
  })
})
