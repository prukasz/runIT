import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { DataConnectorsEditor, DataConnectorsPalette, useDataConnectorsWorkspace } from '../src/DataConnectorsWorkspace'
import type { DataConnectorsWorkspace } from '../src/DataConnectorsWorkspace'

/* Until settings are synced with the board, what was removed from a system stream can be put back. */

function Harness({ part, strip }: { part: 'editor' | 'palette'; strip?: boolean }) {
  const base = useDataConnectorsWorkspace()
  const connectors = strip
    ? base.connectors.map((connector) => (connector.key === 'telemetry' ? { ...connector, bindings: connector.bindings.filter((binding) => binding.provider !== 'UART') } : connector))
    : base.connectors
  const telemetry = connectors.find((connector) => connector.key === 'telemetry')!
  const w: DataConnectorsWorkspace = { ...base, connectors, selectedKey: 'telemetry', selectedConnector: telemetry, modified: !!strip }
  return part === 'editor' ? <DataConnectorsEditor workspace={w} /> : <DataConnectorsPalette workspace={w} />
}

describe('restoring system connectors', () => {
  it('offers nothing while the streams match the board', () => {
    const editor = renderToString(<Harness part="editor" />)
    expect(editor).not.toContain('Restore defaults')
    expect(editor).not.toContain('is-removed')
    expect(renderToString(<Harness part="palette" />)).toMatch(/aria-label="Restore the system connectors of the board" disabled=""/)
  })

  it('shows a removed transport with a button to put it back, and restores the stream or all of them', () => {
    const editor = renderToString(<Harness part="editor" strip />)
    expect(editor).toContain('conn-binding-row is-removed')
    expect(editor).toContain('>removed<')
    expect(editor).toMatch(/aria-label="Restore Serial /)
    expect(editor).toContain('Restore defaults')
    expect(renderToString(<Harness part="palette" strip />)).not.toMatch(/aria-label="Restore the system connectors of the board" disabled=""/)
  })
})
