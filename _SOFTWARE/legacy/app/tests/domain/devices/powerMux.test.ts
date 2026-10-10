import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { boardDeviceRef } from '../../../src/domain/project'
import type { ActionStep, ProjectDevice } from '../../../src/domain/project'
import { checkDevices, checkSetup, contractFrame, defaultInstall, deviceInstallSteps, deviceSetupSteps, findContract, installFrame, isSetupContract, pinUsers, resolveDevice } from '../../../src/domain/devices'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitDeviceCatalog()
const SELECT = 'packet_sys_power_supply_select_t'
const GET = 'packet_sys_power_supply_get_source_t'

const mux = (extra: Record<string, number> = {}): ProjectDevice => {
  const type = catalog.type('device_power_mux')!
  return { id: 'mux', deviceId: 20, type: type.id, name: 'Load supply', tags: [], install: { ...defaultInstall(type, catalog.i2cBuses.user), en_a_pin_device_id: 0, en_a_pin_pin: 10, en_b_pin_device_id: 0, en_b_pin_pin: 11, ...extra } }
}

describe('power mux device', () => {
  it('is a device type with the power supply contract: select is a default setting, reading is not', () => {
    const type = catalog.type('device_power_mux')!
    expect(type.contracts.map((contract) => contract.id)).toEqual([SELECT, GET])
    const device = resolveDevice(catalog, [mux()], 'mux')!
    const select = findContract(catalog, device, SELECT)!
    expect(isSetupContract(select)).toBe(true)
    expect(isSetupContract(findContract(catalog, device, GET)!)).toBe(false)
    expect(select.parameters.find((parameter) => parameter.name === 'source')?.choices?.map((choice) => choice.value)).toEqual([0, 1, 2])
  })

  it('installs from two enable pins and sends the chosen source as a default setting after the install', () => {
    const device = mux()
    expect(checkDevices(catalog, [device]).filter((entry) => entry.severity === 'error')).toEqual([])
    const install = deviceInstallSteps(catalog, [device])
    expect(install.diagnostics).toEqual([])
    // class, create, type 0x4A, id 20, enable A = device 0 pin 10 mode 3, enable B = device 0 pin 11 mode 3, active high, 5 ms
    expect(hex(install.steps[0]!.frame)).toBe('01 00 4a 14 00 0a 03 00 0b 03 01 05')
    expect(hex(installFrame(catalog.type('device_power_mux')!, device))).toBe(hex(install.steps[0]!.frame))

    const setup: ActionStep[] = [{ id: 's1', device: 'mux', contract: SELECT, values: { source: 2 } }]
    expect(checkSetup(catalog, [device], setup)).toEqual([])
    expect(deviceSetupSteps(catalog, [device], setup).steps.map((step) => hex(step.frame))).toEqual(['01 3a 14 02'])
    expect(hex(contractFrame(findContract(catalog, resolveDevice(catalog, [device], 'mux')!, SELECT)!, 20, { source: 1 }))).toBe('01 3a 14 01')
  })
})

describe('power muxes of the board', () => {
  // One LM73100 pair per H-bridge VM node: A = the board input, B = the regulator rail of that bridge.
  const expected = [
    { id: 7, name: 'SUPPLY_DRV8962_0', a: 14, b: 15 },
    { id: 8, name: 'SUPPLY_DRV8962_1', a: 13, b: 12 },
  ]

  it('are loaded with the board, each on its own two TCA6424A pins', () => {
    const tca = catalog.board.find((device) => device.type?.id === 'device_tca6424a')!.deviceId
    for (const { id, name, a, b } of expected) {
      const device = catalog.board.find((entry) => entry.deviceId === id)!
      expect(device.name).toBe(name)
      expect(device.installed).toBe(true)
      expect(device.type?.id).toBe('device_power_mux')
      expect(device.pins.map((pin) => [pin.deviceId, pin.pin])).toEqual([[tca, a], [tca, b]])
    }
  })

  it('take their pins from the user, and offer the source as a default setting', () => {
    const tca = catalog.board.find((device) => device.type?.id === 'device_tca6424a')!.deviceId
    const users = pinUsers(catalog, [])
    for (const { name, a, b } of expected) for (const pin of [a, b]) expect(users.get(`${tca}:${pin}`)?.[0]?.ownerName).toBe(name)
    const board = resolveDevice(catalog, [], boardDeviceRef(8))!
    expect(isSetupContract(findContract(catalog, board, SELECT)!)).toBe(true)
    expect(hex(contractFrame(findContract(catalog, board, SELECT)!, 8, { source: 2 }))).toBe('01 3a 08 02')
  })
})
