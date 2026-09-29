import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog, runitVmCatalog } from '../../domain/descriptors'
import { createProject, parseProject, serializeProject } from '../../domain/project'
import { blockDevices, blockDevicePins } from './blockDevicePins'
import { blockShape, blockSummary } from './blockView'
import { withDeviceAliases } from '../../domain/devices'

describe('block hardware settings', () => {
  const catalog = runitDeviceCatalog()
  const type = runitVmCatalog().block('IO_SET_LEVEL')!
  it('detailed IO content shows only annotated device and selected pin fields', () => {
    const block = { id: 'io', type: type.key, settings: { device_id: 0, default_io_num: 4, flags: 4, allowed_mask: '0xffff', disabled_action: 'HOLD' } }
    expect(blockSummary(type, block)).toEqual(['Device: GPIO_ESP (#0)', 'Pin: 4'])
    expect(blockSummary(type, block, [], withDeviceAliases(catalog, { 'board:0': 'Front switches' }))).toEqual(['Device: Front switches (#0)', 'Pin: 4'])
    const device = { id: 'custom', deviceId: 20, name: 'Workshop IO', type: 'device_tca6424a', tags: [], install: {} }
    expect(blockSummary(type, { ...block, settings: { ...block.settings, device_id: 20 }, dynamicInputs: [1] }, [device])).toEqual(['Device: Workshop IO (#20)', 'Pin: Dynamic (default 4)'])
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
    expect(blockShape(type, block).inputs.map((pin) => pin.index)).toEqual([0])
    const selected = { ...block, dynamicInputs: [1], settings: { allowed_mask: '0x8000000000000020' } }
    expect(blockShape(type, selected).inputs.map((pin) => pin.index)).toEqual([0, 1])
    expect(blockShape(type, { ...block, inputs: [null, { root: 'selector' }] }).inputs.map((pin) => pin.index)).toEqual([0, 1])
    const project = { ...createProject('pins'), canvases: [{ id: 'main', name: 'Main', blocks: [selected] }] }
    expect(parseProject(serializeProject(project)).canvases).toEqual(project.canvases)
  })
})
