import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog, runitVmCatalog } from '../../../src/domain/descriptors'
import { eventSourceChannels, eventSourceDevices, eventSourceEvents } from '../../../src/domain/devices'
import { compileProgram } from '../../../src/domain/compiler'
import { createProject } from '../../../src/domain/project'
import { blockDeviceLine } from '../../../src/canvas/blocks/blockView'

const catalog = runitDeviceCatalog()
const DOMAIN = 'sys_event_domain_e'
const IO = 0
const POWER = 1
const HBRIDGE = 2
const labels = (choices: readonly { label: string }[]) => choices.map((choice) => choice.label)

describe('event sources', () => {
  it('lists the installed devices that publish in a domain, and every one for any domain', () => {
    expect(eventSourceDevices(catalog, [], DOMAIN, POWER).map((device) => device.name)).toEqual(['TPS55289_2', 'TPS55289_1', 'INA3221'])
    expect(eventSourceDevices(catalog, [], DOMAIN, IO).map((device) => device.name)).toEqual(['GPIO_ESP', 'TCA6424A', 'ADS7128'])
    expect(eventSourceDevices(catalog, [], DOMAIN, 3)).toEqual([])
    expect(eventSourceDevices(catalog, [], DOMAIN, undefined).map((device) => device.name)).toContain('DRV8962_1')
    expect(eventSourceDevices(catalog, [], DOMAIN, undefined).map((device) => device.name)).not.toContain('DRV8962_0')
  })

  it("offers only the channels and events a device declares, and the domain's events when no device is chosen", () => {
    const monitor = catalog.board.find((device) => device.name === 'INA3221')!.deviceId
    expect(eventSourceChannels(catalog, [], DOMAIN, POWER, monitor)).toMatchObject({ label: 'Monitor Channel', choices: [{ value: 0 }, { value: 1 }, { value: 2 }] })
    expect(labels(eventSourceEvents(catalog, [], DOMAIN, POWER, monitor))).toEqual(['Over-Current Critical', 'Over-Current Warning'])
    expect(eventSourceEvents(catalog, [], DOMAIN, POWER, undefined).length).toBeGreaterThan(eventSourceEvents(catalog, [], DOMAIN, POWER, monitor).length)
    const regulator = catalog.board.find((device) => device.name === 'TPS55289_1')!.deviceId
    expect(eventSourceChannels(catalog, [], DOMAIN, POWER, regulator).choices).toEqual([])
    const expander = catalog.board.find((device) => device.name === 'TCA6424A')!.deviceId
    expect(eventSourceChannels(catalog, [], DOMAIN, IO, expander).choices).toHaveLength(24)
    expect(eventSourceEvents(catalog, [], DOMAIN, HBRIDGE, expander)).toEqual([])
  })

  it('narrows nothing for any domain or device', () => {
    expect(eventSourceEvents(catalog, [], DOMAIN, undefined, undefined)).toEqual([])
    expect(eventSourceChannels(catalog, [], DOMAIN, undefined, 12).choices).toEqual([])
    expect(eventSourceChannels(catalog, [], DOMAIN, IO, undefined).choices).toEqual([])
  })
})

describe('a block whose settings start at any', () => {
  const vm = runitVmCatalog()
  const type = vm.block('ON_EVENT')!

  it('describes its source fields in the descriptor', () => {
    expect(type.fields.filter((field) => field.anyValue !== undefined).map((field) => [field.name, field.idKind, field.sourceField, field.deviceField])).toEqual([
      ['domain', undefined, undefined, undefined],
      ['device_id', 'device', 'domain', undefined],
      ['channel', 'channel', 'domain', 'device_id'],
      ['event', 'event', 'domain', 'device_id'],
    ])
    expect(blockDeviceLine(type, { id: 'e', type: 'ON_EVENT' })).toBe('Any device')
  })

  it('compiles with every filter at the wildcard until one is chosen', () => {
    const state = (settings?: Record<string, number | string>) => {
      const compiled = compileProgram(createProject('p'), vm, { maxFrameBytes: 244, blocks: [{ id: 'e', type: 'ON_EVENT', ...(settings ? { settings } : {}) }] })
      expect(compiled.diagnostics).toEqual([])
      const record = compiled.frames.find((frame) => frame[1] === vm.packets.addBlock)!
      return [...record.subarray(record.length - 4)]
    }
    expect(state()).toEqual([255, 255, 255, 255])
    expect(state({ domain: 'POWER', device_id: 12, channel: 1, event: 4 })).toEqual([1, 12, 1, 4])
  })
})
