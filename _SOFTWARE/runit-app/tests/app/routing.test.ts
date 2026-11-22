import { describe, expect, it } from 'vitest'
import { bundledLinkDefinitions } from '@backend'
import {
  decodeMatrixLinks,
  decodeProviderRows,
  modulePort,
  offlinePreviewSnapshot,
  parsePort,
  PROV_STATE,
  PROV_TYPE,
  providerPort,
  providerTitle,
  sessionPort,
  standardModulePorts,
  withProviderNames,
} from '../../src/app/routing/routing'
import { chipLinks, chipState, deadmanChoices, deadmanLabel, sessionModules, subPortLinks } from '../../src/app/routing/connections'

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
    expect(providerTitle(rows[0]!)).toContain('#1')
  })

  it("a chip is the links a module and a provider's data path need: its out to it and its in from it", () => {
    const preview = offlinePreviewSnapshot(defs)
    const modules = standardModulePorts(defs)
    const logs = modules.find((m) => m.moduleId === defs.keys.log)!
    const watch = modules.find((m) => m.moduleId === defs.keys.watch)!
    const router = modules.find((m) => m.moduleId === defs.keys.router)!
    // Logs only send, the router only takes: one link each; Watch both ways.
    expect(chipLinks(logs, { kind: 'provider', provider: 4 })).toEqual([{ from: modulePort(defs.keys.log!), to: providerPort(4) }])
    expect(chipLinks(router, { kind: 'session', provider: 3 })).toEqual([{ from: sessionPort(3, defs.keys.router!), to: modulePort(defs.keys.router!) }])
    expect(chipLinks(watch, { kind: 'provider', provider: 5 })).toHaveLength(2)
    expect(chipState(preview.links, chipLinks(logs, { kind: 'provider', provider: 4 }))).toBe('on')
    expect(chipState(preview.links, chipLinks(logs, { kind: 'provider', provider: 5 }))).toBe('off')
    const half = [{ from: modulePort(defs.keys.watch!), to: providerPort(9) }]
    expect(chipState(half, chipLinks(watch, { kind: 'provider', provider: 9 }))).toBe('partial')
  })

  it('knows the modules of a session by the links of its sub-ports, and which links a restart must make again', () => {
    const links = offlinePreviewSnapshot(defs).links
    const mods = sessionModules(links, 3)
    expect(mods).toContain(defs.keys.router)
    expect(subPortLinks(links, 3).length).toBeGreaterThan(0)
    expect(subPortLinks(links, 4)).toEqual([])
  })

  it("offers the dead-man times, with the session's own when it is none of them", () => {
    expect(deadmanChoices(3000)).toEqual([0, 1000, 3000, 10000, 30000, 60000])
    expect(deadmanChoices(5000)).toEqual([0, 1000, 3000, 5000, 10000, 30000, 60000])
    expect([0, 1000, 1500].map(deadmanLabel)).toEqual(['Off', '1 s', '1500 ms'])
  })

  it("calls a provider by the project's name with its id, else by its id and type", () => {
    const snapshot = offlinePreviewSnapshot(defs)
    const named = withProviderNames(snapshot, { '3': 'Phone link', '4': '  ' })
    expect(named.providers.find((p) => p.id === 3)!.name).toBe('Phone link')
    expect(named.providers.find((p) => p.id === 4)!.name).toBeUndefined()
    expect(providerTitle(named.providers.find((p) => p.id === 3)!)).toBe('Phone link (#3)')
    expect(providerTitle(named.providers.find((p) => p.id === 4)!)).toMatch(/^Provider #4 \(/)
    expect(withProviderNames(snapshot, undefined)).toBe(snapshot)
  })
})
