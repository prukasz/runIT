import { describe, expect, it } from 'vitest'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from '../descriptors'
import { createProject, parseProject, serializeProject } from '../project'
import type { BleCharacteristicSettings, ConnectorSettings, ProjectDocument, ProjectSettings } from '../project'
import { buildStoredCode, decodeStoredCode } from '../storedCode'
import type { StoredCodeContext } from '../storedCode'
import { boardDefaultSettings, planSettingsUpload, runitSettingsIds } from '../upload'
import { defaultProjectSettings, isBoardCharacteristic, refreshSettings, settingsFromState, settingsState } from '.'

const ctx: StoredCodeContext = { vm: runitVmCatalog(), commands: runitCommandCatalog(), layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: runitDeviceCatalog() }
const boardDefaults = boardDefaultSettings()

const char = (id: string, uuid: string, extra: Partial<BleCharacteristicSettings> = {}): BleCharacteristicSettings => ({
  id, name: id, uuid, system: false, read: false, write: false, writeNoResponse: false, notify: true, indicate: false, txBufferSize: 64, rxBufferSize: 0, format: 'RAW', ...extra,
})

/** Defaults plus a user service, a user connector and a system connector change. */
const edited = (): ProjectSettings => {
  const defaults = defaultProjectSettings()
  const system = defaults.ble.services[0]!
  const telemetry = defaults.connectors.find((connector) => connector.key === 'telemetry')!
  const user: ConnectorSettings = {
    id: ctx.ids.appConnectorBase, key: 'app', name: 'app', alias: 'App', header: '0x10', system: false, description: 'mine', direction: 'TX',
    maxPacketLen: 128, isSuspended: false, cMacro: 'SYS_DATA_CONNECTOR_APP_4', bindings: [{ id: 'b1', provider: 'BLE', endpoint: '0xFF11', direction: 'TX' }],
  }
  return {
    ble: {
      ...defaults.ble,
      general: { ...defaults.ble.general, deviceName: 'rover' },
      services: [
        system,
        { id: 's-user', name: 'user', uuid: '0xFF10', system: false, isPrimary: true, advertised: false, characteristics: [char('c-a', '0xFF11'), char('c-extra', '0xFF30', { description: 'extra', format: 'F' })] },
      ],
    },
    connectors: [...defaults.connectors.map((connector) => (connector === telemetry ? { ...connector, isSuspended: true } : connector)), user],
  }
}

describe('project file with settings', () => {
  const project: ProjectDocument = {
    ...createProject('rover'),
    autostart: true,
    settings: edited(),
    extraFrames: [{ label: 'power', frame: Uint8Array.from([0x08, 0x01, 0x02]) }],
  }

  it('saves and loads back settings, autostart and extra frames', () => {
    const text = serializeProject(project)
    expect(parseProject(text)).toEqual(project)
    expect(serializeProject(parseProject(text))).toBe(text)
    expect(text).toContain('"frame": "08 01 02"')
  })

  it('names bad settings values', () => {
    const broken = JSON.parse(serializeProject(project))
    broken.settings.connectors[0].bindings[0].direction = 'UP'
    expect(() => parseProject(JSON.stringify(broken))).toThrow(/^settings\.connectors\[0\]\.bindings\[0\]\.direction:/)
    broken.settings.connectors[0].bindings[0].direction = 'TX'
    broken.extra_frames[0].frame = '08'
    expect(() => parseProject(JSON.stringify(broken))).toThrow(/^extra_frames\[0\]\.frame:/)
  })
})

describe('settings refresh and recovery', () => {
  it('refresh takes the board entries from the descriptors and keeps the user entries', () => {
    const saved = edited()
    const stale: ProjectSettings = {
      ...saved,
      ble: { ...saved.ble, services: saved.ble.services.map((service, index) => (index === 0 ? { ...service, name: 'old name', characteristics: service.characteristics.map((c) => (c.system ? { ...c, txBufferSize: 1 } : c)) } : service)) },
      connectors: saved.connectors.map((connector) => (connector.system ? { ...connector, name: 'renamed', header: '0x7F' } : connector)),
    }
    const refreshed = refreshSettings(stale)
    expect(refreshed).toEqual(saved)
  })

  it('stored code → editor settings: nothing to apply against the edited settings', () => {
    const settings = edited()
    const code = buildStoredCode({ project: createProject('x'), settings: settingsState(settings), boardDefaults }, ctx)
    expect(code.ok).toBe(true)
    const recovered = decodeStoredCode(code.steps.map((step) => step.frame), boardDefaults, ctx)
    const back = settingsFromState(recovered.settings, settings)
    const plan = planSettingsUpload(ctx.commands, ctx.layout, ctx.ids, settingsState(settings), settingsState(back))
    expect(plan.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
    expect(plan.steps).toEqual([])
    expect(back.ble.general.deviceName).toBe('rover')
    expect(back.connectors.find((connector) => connector.key === 'telemetry')?.isSuspended).toBe(true)
  })

  it('refuses a characteristic in the locked board service', () => {
    const defaults = defaultProjectSettings()
    const system = defaults.ble.services[0]!
    const extra: ProjectSettings = { ...defaults, ble: { ...defaults.ble, services: [{ ...system, characteristics: [...system.characteristics, char('c-x', '0xFFE5')] }] } }
    const plan = planSettingsUpload(ctx.commands, ctx.layout, ctx.ids, boardDefaults, settingsState(extra))
    expect(plan.steps).toEqual([])
    expect(plan.diagnostics.map((entry) => entry.message).join()).toMatch(/service 0xFFE0 is locked/)
    expect(refreshSettings(extra).ble.services[0]).toEqual(system)
  })

  it('a board UUID in a user service is the user’s duplicate, not a board characteristic', () => {
    const defaults = defaultProjectSettings()
    const dup: ProjectSettings = { ...defaults, ble: { ...defaults.ble, services: [...defaults.ble.services, { id: 's-u', name: 'user', uuid: '0xFF20', system: false, isPrimary: true, advertised: false, characteristics: [char('c-dup', '0xFFE3')] }] } }
    expect(isBoardCharacteristic(dup.ble.services[1]!.characteristics[0])).toBe(false)
    const plan = planSettingsUpload(ctx.commands, ctx.layout, ctx.ids, boardDefaults, settingsState(dup))
    expect(plan.diagnostics.map((entry) => entry.message).join()).toMatch(/share UUID 0xFFE3/)
  })

  it('defaults need no stored settings frames', () => {
    const code = buildStoredCode({ project: createProject('x'), settings: settingsState(defaultProjectSettings()), boardDefaults }, ctx)
    expect(code.steps.filter((step) => step.frame[0] !== ctx.vm.classHeader)).toEqual([])
  })
})
