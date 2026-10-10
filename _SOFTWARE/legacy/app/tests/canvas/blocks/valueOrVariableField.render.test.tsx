import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { ValueOrVariableField } from '../../../src/canvas/blocks/ValueOrVariableField'

const noop = () => undefined
const parse = () => ({ root: 'count' })

describe('ValueOrVariableField', () => {
  it('shows the constant while nothing is wired and the variable once it is', () => {
    const constant = renderToString(<ValueOrVariableField label="Start" constant={5} integer={false} parse={parse} onConstant={noop} onVariable={noop} />)
    expect(constant).toContain('value="5"')
    expect(constant).not.toContain('is-variable')
    const wired = renderToString(<ValueOrVariableField label="Start" constant={5} variable={{ root: 'count' }} variableText="count" integer={false} parse={parse} onConstant={noop} onVariable={noop} />)
    expect(wired).toContain('value="count"')
    expect(wired).toContain('is-variable')
  })
})
