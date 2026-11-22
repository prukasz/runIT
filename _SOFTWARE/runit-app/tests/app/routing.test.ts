import { describe, expect, it } from 'vitest'
import { bundledLinkDefinitions } from '@backend'
import {
  buildGraphLayout,
  decodeMatrixLinks,
  decodeProviderRows,
  describePort,
  modulePort,
  offlinePreviewSnapshot,
  parsePort,
  PROV_STATE,
  PROV_TYPE,
  providerPort,
  sessionPort,
} from '../../src/app/routing/routing'

const defs = bundledLinkDefinitions()

describe('routing model (Providers & Data Matrix)', () => {
  it('encodes and parses module, provider and session ports', () => {
    const mp = modulePort(defs.keys.router!)
    expect(parsePort(mp)).toEqual({ kind: 'module', moduleId: defs.keys.router })

    const pp = providerPort(3)
    expect(parsePort(pp)).toEqual({ kind: 'provider', providerId: 3 })

    const spLine = sessionPort(3, 0)
    expect(parsePort(spLine)).toEqual({ kind: 'session', providerId: 3, moduleId: 0 })

    const spSub = sessionPort(3, defs.keys.sys_event!)
    expect(parsePort(spSub)).toEqual({ kind: 'session', providerId: 3, moduleId: defs.keys.sys_event })
  })

  it('decodes DATA_MATRIX LIST and PROVIDER LIST wire payloads', () => {
    const a = modulePort(defs.keys.log!)
    const b = providerPort(4)
    const linksRaw = Uint8Array.of(1, a & 0xff, a >> 8, b & 0xff, b >> 8)
    expect(decodeMatrixLinks(linksRaw)).toEqual([{ from: a, to: b }])

    const provRaw = Uint8Array.of(
      2,
      1, PROV_TYPE.console, PROV_STATE.system | PROV_STATE.wired | PROV_STATE.up | PROV_STATE.framed,
      8, PROV_TYPE.ble, PROV_STATE.framed,
    )
    const rows = decodeProviderRows(provRaw)
    expect(rows).toHaveLength(2)
    expect(rows[0]).toMatchObject({ id: 1, system: true, wired: true, up: true, framed: true })
    expect(rows[1]).toMatchObject({ id: 8, system: false, wired: false, up: false, framed: true })
    expect(describePort(providerPort(1), rows, defs)).toContain('#1')
  })

  it('builds a 3-column graph layout with wired and unwired endpoints', () => {
    const preview = offlinePreviewSnapshot(defs)
    const layout = buildGraphLayout(preview, defs)
    expect(layout.modules.length).toBeGreaterThanOrEqual(6)
    expect(layout.sessions).toHaveLength(2)
    expect(layout.providers).toHaveLength(4)
    expect(layout.edges.length).toBe(preview.links.length)
    expect(layout.edges.some((e) => e.kind === 'session-provider')).toBe(true)
    expect(layout.edges.some((e) => e.kind === 'module-session')).toBe(true)
    expect(layout.edges.some((e) => e.kind === 'direct')).toBe(true)
  })
})
