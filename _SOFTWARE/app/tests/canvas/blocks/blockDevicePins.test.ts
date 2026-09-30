import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog, runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject, parseProject, serializeProject } from '../../../src/domain/project'
import { blockDevices, blockDevicePins } from '../../../src/canvas/blocks/blockDevicePins'
import { blockDeviceLine, blockHeadline, blockShape } from '../../../src/canvas/blocks/blockView'
import { withDeviceAliases } from '../../../src/domain/devices'

describe('block hardware settings', () => {
  const catalog = runitDeviceCatalog()
  const type = runitVmCatalog().block('IO_SET_LEVEL')!
  it('shows the pin of an IO block on its face and its device on the line below, from the descriptor', () => {
    const block = { id: 'io', type: type.key, settings: { device_id: 0, default_io_num: 4, allowed_mask: '0xffff', when_not_active: 'HOLD' } }
    expect(blockHeadline(type, block)).toBe('Set Pin Level #4')
    expect(blockDeviceLine(type, block)).toBe('GPIO_ESP (#0)')
    expect(blockDeviceLine(type, block, [], withDeviceAliases(catalog, { 'board:0': 'Front switches' }))).toBe('Front switches (#0)')
    const device = { id: 'custom', deviceId: 20, name: 'Workshop IO', type: 'device_tca6424a', tags: [], install: {} }
    const dynamic = { ...block, settings: { ...block.settings, device_id: 20 }, dynamicInputs: [1] }
    expect(blockDeviceLine(type, dynamic, [device])).toBe('Workshop IO (#20)')
    expect(blockHeadline(type, dynamic)).toBe('Set Pin Level dynamic pin')
  })
  it('offers installed devices that answer the block operation and excludes locked/reserved pins', () => {
    const field = type.fields.find((field) => field.idKind === 'device')!
    const targets = blockDevices(catalog, [], field)
    expect(targets.length).toBeGreaterThan(0)
    expect(targets.every((target) => target.type?.contracts.some((contract) => contract.id === field.contract))).toBe(true)
    const gpio = targets.find((target) => target.type?.id === 'device_gpio_esp')!
    const pins = blockDevicePins(catalog, [], gpio.deviceId, field.contract)
    for (const reserved of catalog.reservedPins.filter((pin) => pin.deviceId === gpio.deviceId)) expect(pins.some((pin) => pin.value === reserved.pin)).toBe(false)
    for (const board of catalog.board.filter((board) => board.installed)) for (const pin of board.pins.filter((pin) => pin.deviceId === gpio.deviceId)) expect(pins.some((entry) => entry.value === pin.pin)).toBe(false)
  })
  it('preserves hidden input indices, exposes selected or wired dynamic pins, and saves the selection', () => {
    const block = { id: 'io', type: type.key, x: 0, y: 0 }
    expect(blockShape(type, block).inputs.map((pin) => pin.index)).toEqual([0, 1]) // always detailed: Level and Pin both drawn, with their constants
    const selected = { ...block, dynamicInputs: [1], settings: { allowed_mask: '0x8000000000000020' } }
    expect(blockShape(type, selected).inputs.map((pin) => pin.index)).toEqual([0, 1])
    expect(blockShape(type, { ...block, inputs: [null, { root: 'selector' }] }).inputs.map((pin) => pin.index)).toEqual([0, 1])
    const project = { ...createProject('pins'), canvases: [{ id: 'main', name: 'Main', blocks: [selected] }] }
    expect(parseProject(serializeProject(project)).canvases).toEqual(project.canvases)
  })
})
