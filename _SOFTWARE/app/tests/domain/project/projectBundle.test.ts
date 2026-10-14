import { describe, expect, it } from 'vitest'
import { emptyBundle, parseBundle, serializeBundle } from '../../../src/domain/project'
import { boardConnectors, defaultBleProfile } from '../../../src/domain/settings'

describe('the project file (objects, canvases, settings)', () => {
  it('reads back what it wrote', () => {
    const base = emptyBundle('Rover')
    const bundle = {
      ...base,
      project: { ...base.project, objects: [{ kind: 'value' as const, id: 'obj-1', name: 'speed', type: 'F', length: 1, mutable: true, retentive: false }] },
      canvases: [{ id: 'canvas-1', name: 'Main', blocks: [] }],
      settings: {
        ...base.settings,
        ble: { ...defaultBleProfile(), services: [...defaultBleProfile().services, { id: 's', name: 'Sensors', uuid: '0xEE00', isPrimary: true, characteristics: [] }] },
        routers: { ...base.settings.routers, ROUTER_KEY_POWER: { ...base.settings.routers.ROUTER_KEY_POWER, SYS_PWRM_OP_SET_BATTERY: { chemistry: 1, cells: 3, low_pct: 25, critical_pct: 10 } } },
      },
    }
    const back = parseBundle(serializeBundle(bundle as never))
    expect(back.project.name).toBe('Rover')
    expect(back.project.objects.map((node) => node.name)).toEqual(['speed'])
    expect(back.canvases.map((canvas) => canvas.name)).toEqual(['Main'])
    expect(back.settings.ble.services.map((service) => service.uuid)).toEqual(['0xFFE0', '0xEE00'])
    expect(back.settings.routers.ROUTER_KEY_POWER!.SYS_PWRM_OP_SET_BATTERY).toEqual({ chemistry: 1, cells: 3, low_pct: 25, critical_pct: 10 })
  })

  it('opens a file of the object tree alone: the rest starts from the defaults', () => {
    const back = parseBundle(JSON.stringify({ format: 'runit-project', format_version: 1, name: 'Old', objects: [] }))
    expect(back.canvases).toEqual([])
    expect(back.settings.connectors).toEqual(boardConnectors())
    expect(back.settings.routers.ROUTER_KEY_LOG!.SYS_LOG_OP_SET).toMatchObject({ level: 3 })
  })

  it('reads back pinAliases if present in devices', () => {
    const base = emptyBundle('Bot')
    const bundle = {
      ...base,
      devices: {
        ...base.devices,
        pinAliases: {
          'board:0': { '4': 'Buzzer' },
        },
      },
    }
    const back = parseBundle(serializeBundle(bundle as never))
    expect(back.devices.pinAliases).toEqual({
      'board:0': { '4': 'Buzzer' },
    })
  })

  it('refuses a file that is not a project', () => {
    expect(() => parseBundle('{"format":"something"}')).toThrow(/format/)
  })
})
