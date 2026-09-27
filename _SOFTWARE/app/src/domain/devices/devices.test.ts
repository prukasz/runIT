import { describe, expect, it } from 'vitest'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from '../descriptors'
import { boardDeviceRef, createProject, parseProject, serializeProject } from '../project'
import type { ActionStep, ProjectAction, ProjectDevice } from '../project'
import { buildStoredCode, decodeStoredCode } from '../storedCode'
import { boardDefaultSettings, runitSettingsIds } from '../upload'
import { actionRecordSteps, buildAction, checkDevices, checkPwm, frequencyWarnings, checkSetup, clampErrorActions, decodeErrorActions, encodeErrorActions, reachableErrorLevels, contractFrame, defaultInstall, deviceInstallSteps, findContract, installFrame, nextDeviceId, pinKey, pinsOf, pinUsers, resolveDevice } from '.'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitDeviceCatalog()
const commands = runitCommandCatalog()

const pca = (deviceId: number, extra: Record<string, number> = {}): ProjectDevice => {
  const type = catalog.type('device_pca9685')!
  return { id: `pca-${deviceId}`, deviceId, type: type.id, name: `pwm ${deviceId}`, tags: ['servo'], install: { ...defaultInstall(type, catalog.i2cBuses.user), i2c_addr: 0x41, ...extra } }
}

describe('device catalog', () => {
  it('loads every device type, the board devices and the lifecycle commands', () => {
    expect(catalog.types.map((type) => type.id)).toEqual(expect.arrayContaining(['device_gpio_esp', 'device_pca9685', 'device_tca6424a', 'device_ina3221']))
    expect(catalog.board.find((device) => device.deviceId === 3)?.type?.id).toBe('device_pca9685')
    expect(catalog.lifecycle.map((contract) => contract.command.name)).toEqual(expect.arrayContaining(['sys_device_uninstall', 'sys_device_reset']))
    expect(catalog.type('device_pca9685')?.datasheet).toMatch(/^https:\/\/.*\.pdf$/i)
  })

  it('marks the device ID as filled from the device and reads pin groups', () => {
    const type = catalog.type('device_pca9685')!
    const setLevel = type.contracts.find((contract) => contract.id === 'packet_sys_io_set_level_t')!
    expect(setLevel.parameters.find((parameter) => parameter.name === 'device_id')?.instance).toBe(true)
    expect(setLevel.parameters.find((parameter) => parameter.name === 'pin')?.choices?.length).toBe(16)
    expect(type.pinGroups).toEqual([expect.objectContaining({ pinField: 'oe_pin_pin', deviceField: 'oe_pin_device_id', modeField: 'oe_pin_mode', sentinel: 255 })])
  })
})

describe('device-wide settings and board limits', () => {
  it('sends a device-wide PCA frequency with the channel field hidden and 0', () => {
    const board = resolveDevice(catalog, [], boardDeviceRef(3))!
    const frequency = findContract(catalog, board, 'packet_sys_io_set_pwm_frequency_t')!
    expect(frequency.parameters.find((parameter) => parameter.name === 'pin')?.deviceWide).toBe(true)
    expect(frequency.parameters.find((parameter) => parameter.name === 'frequency_Hz')).toMatchObject({ min: 24, max: 1526, defaultValue: 50 })
    expect(hex(contractFrame(frequency, 3, { pin: 7, frequency_Hz: 50 }))).toBe('01 27 03 07 32 00 00 00')
    expect(hex(contractFrame(frequency, 3, { frequency_Hz: 50 }))).toBe('01 27 03 00 32 00 00 00')
    expect(findContract(catalog, board, 'packet_sys_io_set_pwm_duty_t')?.parameters.find((parameter) => parameter.name === 'duty')?.max).toBe(4095)
  })
})

describe('PWM frequency warnings', () => {
  const frequency = (device: string, hz: number, pin = 0): ActionStep => ({ id: `f-${device}-${hz}-${pin}`, device, contract: 'packet_sys_io_set_pwm_frequency_t', values: { pin, frequency_Hz: hz } })

  it('warns that a PCA frequency changes every channel in use, and about a second frequency', () => {
    const live = frequencyWarnings(catalog, [], [], [], { device: boardDeviceRef(3), hz: 50 })
    expect(live.join()).toMatch(/PCA9685 has one PWM frequency for all its pins: 50 Hz also changes DRV8962_1 \(pins 8–15\)/)
    const servo: ActionStep = { id: 'd', device: boardDeviceRef(3), contract: 'packet_sys_io_set_pwm_duty_t', values: { pin: 0, duty: 300 } }
    const action: ProjectAction = { id: 'a1', actionId: 3, name: 'fast', steps: [frequency(boardDeviceRef(3), 1000)] }
    const warnings = checkPwm(catalog, [], [frequency(boardDeviceRef(3), 50), servo], [action])
    expect(warnings.every((entry) => entry.severity === 'warning')).toBe(true)
    expect(warnings.find((entry) => entry.subjectId === 'a1' && /default settings \(pins 0\)/.test(entry.message))).toBeTruthy()
    expect(warnings.find((entry) => entry.subjectId === `setup:${boardDeviceRef(3)}` && /also set to 1000 Hz \(action 'fast'\)/.test(entry.message))).toBeTruthy()
  })

  it('warns when ESP pins need more frequencies than it has timers, never for pins that share one', () => {
    const esp = boardDeviceRef(0)
    expect(catalog.type('device_gpio_esp')?.pwmFrequencies).toBe(4)
    const four = [frequency(esp, 100, 10), frequency(esp, 200, 11), frequency(esp, 300, 12), frequency(esp, 400, 13), frequency(esp, 400, 14)]
    expect(checkPwm(catalog, [], four, [])).toEqual([])
    const five = [...four, frequency(esp, 500, 17)]
    expect(checkPwm(catalog, [], five, [])[0]?.message).toMatch(/at most 4 different PWM frequencies at once .* uses 5 \(100, 200, 300, 400, 500 Hz\)/)
    expect(frequencyWarnings(catalog, [], four, [], { device: esp, pin: 18, hz: 600 }).join()).toMatch(/uses 5/)
    expect(frequencyWarnings(catalog, [], four, [], { device: esp, pin: 13, hz: 600 }).join()).toMatch(/uses 5/) // pin 14 keeps the 400 Hz timer
    expect(frequencyWarnings(catalog, [], four, [], { device: esp, pin: 12, hz: 600 })).toEqual([]) // 300 Hz was pin 12's alone
  })
})

describe('device frames', () => {
  it('packs install and contract frames with the device ID from the device', () => {
    const device = pca(20)
    expect(hex(installFrame(catalog.type('device_pca9685')!, device))).toBe('01 41 14 01 41 00 ff 00')
    const board = resolveDevice(catalog, [], boardDeviceRef(0))!
    const toggle = findContract(catalog, board, 'packet_sys_io_toggle_t')!
    expect(hex(contractFrame(toggle, board.deviceId, { pin: 4, device_id: 99 }))).toBe('01 24 00 04')
  })

  it('gives new devices the first free ID after the board and checks clashes', () => {
    const first = nextDeviceId(catalog, [])!
    expect(first).toBe(Math.max(...catalog.board.map((device) => device.deviceId)) + 1)
    expect(nextDeviceId(catalog, [pca(first)])).toBe(first + 1)
    expect(checkDevices(catalog, [pca(3)])[0]?.message).toMatch(/board's PCA9685/)
    expect(checkDevices(catalog, [pca(20), pca(21)]).map((entry) => entry.message).join()).toMatch(/also 'pwm 20'/)
    expect(checkDevices(catalog, [pca(20)])).toEqual([])
  })

  it('stores user devices in the code and recovers them', () => {
    const ctx = { vm: runitVmCatalog(), commands, layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: catalog }
    const defaults = boardDefaultSettings()
    const devices = [pca(20)]
    const setup: ActionStep[] = [{ id: 'm', device: boardDeviceRef(0), contract: 'packet_sys_io_set_mode_t', values: { pin: 5, mode: 3 } }, { id: 'l', device: 'pca-20', contract: 'packet_sys_io_set_pwm_frequency_t', values: { pin: 0, frequency_Hz: 50 } }]
    const code = buildStoredCode({ project: createProject('x'), settings: defaults, boardDefaults: defaults, devices, setup }, ctx)
    expect(code.ok).toBe(true)
    expect(code.steps.map((step) => step.label)).toContain('device install pwm 20 (20)')
    const recovered = decodeStoredCode(code.steps.map((step) => step.frame), defaults, ctx)
    expect(recovered.devices).toEqual([expect.objectContaining({ deviceId: 20, type: 'device_pca9685', install: devices[0]!.install })])
    expect(recovered.extraFrames).toEqual([])
    expect(recovered.setup.map((step) => [step.device, step.contract, step.values])).toEqual([[boardDeviceRef(0), 'packet_sys_io_set_mode_t', { pin: 5, mode: 3 }], [recovered.devices[0]!.id, 'packet_sys_io_set_pwm_frequency_t', { pin: 0, frequency_Hz: 50 }]])
  })
})

describe('install order', () => {
  const tca = (deviceId: number, extra: Record<string, number> = {}): ProjectDevice => {
    const type = catalog.type('device_tca6424a')!
    return { id: `tca-${deviceId}`, deviceId, type: type.id, name: `io ${deviceId}`, tags: [], install: { ...defaultInstall(type, catalog.i2cBuses.user), i2c_addr: 0x23, ...extra } }
  }

  it('installs by device ID; a device needs its pins on a lower-ID device', () => {
    // Listed out of order: the PCA (22) has its OE pin on the user TCA (21).
    const devices = [pca(22, { oe_pin_device_id: 21, oe_pin_pin: 3, oe_pin_mode: 3 }), tca(21), pca(20, { i2c_addr: 0x42 })]
    expect(deviceInstallSteps(catalog, devices).steps.map((step) => step.label)).toEqual(['device install pwm 20 (20)', 'device install io 21 (21)', 'device install pwm 22 (22)'])
    // The board suspends from the highest ID down: a pin on a higher-ID device is refused.
    const upward = deviceInstallSteps(catalog, [pca(20, { oe_pin_device_id: 21, oe_pin_pin: 3, oe_pin_mode: 3 }), tca(21)])
    expect(upward.steps).toEqual([])
    expect(upward.diagnostics.map((entry) => entry.message).join()).toMatch(/on io 21 \(ID 21\): a device's pins must be on a device with a lower ID/)
  })
})

describe('pins', () => {
  it('knows which device takes which pin, from the board wiring and the user devices', () => {
    const users = pinUsers(catalog, [pca(20, { oe_pin_device_id: 0, oe_pin_pin: 5, oe_pin_mode: 3 })])
    expect(users.get(pinKey(1, 0))?.[0]).toMatchObject({ owner: boardDeviceRef(3), ownerName: 'PCA9685' })
    expect(users.get(pinKey(1, 22))?.[0]).toMatchObject({ ownerName: 'Board', use: 'Status LEDs (high = on)' })
    expect(users.get(pinKey(0, 40))?.[0]).toMatchObject({ ownerName: 'Board', use: 'I2C 1 SDA (external)' })
    expect(users.get(pinKey(0, 5))?.[0]).toMatchObject({ owner: 'pca-20', mode: 3 })
    expect(users.has(pinKey(0, 21))).toBe(false)
    expect(pinsOf(catalog.type('device_gpio_esp')).map((pin) => pin.value)).toContain(48)
  })

  it('refuses a pin another device takes, and a device on the internal bus', () => {
    expect(checkDevices(catalog, [pca(20, { oe_pin_device_id: 1, oe_pin_pin: 0, oe_pin_mode: 3 })])[0]?.message).toMatch(/TCA6424A pin 0 is taken by PCA9685/)
    expect(checkDevices(catalog, [pca(20, { i2c_bus: 0 })])[0]?.message).toMatch(/user I2C bus 1/)
    const onLed = [{ id: 's', device: boardDeviceRef(1), contract: 'packet_sys_io_set_mode_t', values: { pin: 22, mode: 0 } }]
    expect(checkSetup(catalog, [], onLed)[0]?.message).toMatch(/taken by Board/)
  })
})

describe('actions', () => {
  const action: ProjectAction = {
    id: 'a1',
    actionId: 7,
    name: 'lights on',
    steps: [
      { id: 's1', device: boardDeviceRef(0), contract: 'packet_sys_io_set_level_t', values: { pin: 1, level: 1 } },
      { id: 's2', device: 'pca-20', contract: 'packet_sys_io_set_level_t', values: { pin: 2, level: 1 } },
    ],
  }

  it('builds the calls, sizes them and wraps them in record start / stop', () => {
    const build = buildAction(catalog, [pca(20)], action)
    expect(build.ok).toBe(true)
    expect(build.frames.map((step) => hex(step.frame))).toEqual(['01 22 00 01 01', '01 22 14 02 01'])
    expect(build.bytes).toBe(14)
    expect(actionRecordSteps(commands, action, build).map((step) => hex(step.frame))).toEqual(['03 02 07', '01 22 00 01 01', '01 22 14 02 01', '03 03'])
  })

  it('packs error handling as the firmware reads it: scope bits, then the action per level', () => {
    // Medium: static action 4 (suspend all); High: recorded action 7; Low, Critical: none.
    const actions = encodeErrorActions([{ scope: 'static', id: 0 }, { scope: 'static', id: 4 }, { scope: 'dynamic', id: 7 }, { scope: 'dynamic', id: 0 }])
    expect(actions).toEqual([0b0100, 0, 4, 7, 0])
    expect(decodeErrorActions(actions)).toEqual([{ scope: 'static', id: 0 }, { scope: 'static', id: 4 }, { scope: 'dynamic', id: 7 }, { scope: 'static', id: 0 }])
    const errorHandling: ProjectAction = { id: 'a2', actionId: 8, name: 'on error', steps: [{ id: 's3', device: boardDeviceRef(13), contract: 'packet_sys_device_set_error_handling_t', values: { importance: 2, actions } }] }
    const build = buildAction(catalog, [], errorHandling)
    expect(build.diagnostics).toEqual([])
    expect(build.frames.map((step) => hex(step.frame))).toEqual(['01 1a 0d 02 04 00 04 07 00'])
    expect(catalog.staticActions.map((entry) => entry.value)).toEqual([1, 2, 3, 4, 5, 6])
    // sys_device.c: above the importance → the importance level; critical always, Disabled included.
    expect([0, 1, 2, 3, 4].map(reachableErrorLevels)).toEqual([[4], [1, 4], [1, 2, 4], [1, 2, 3, 4], [1, 2, 3, 4]])
    expect(clampErrorActions(encodeErrorActions([{ scope: 'dynamic', id: 1 }, { scope: 'dynamic', id: 2 }, { scope: 'static', id: 3 }, { scope: 'static', id: 4 }]), 1)).toEqual([0b0001, 1, 0, 0, 4])
    const project = { ...createProject('x'), actions: [errorHandling] }
    expect(parseProject(serializeProject(project))).toEqual(project)
  })

  it('reports a step whose device is gone', () => {
    expect(buildAction(catalog, [], action).diagnostics[0]?.message).toMatch(/Step 2: device pca-20 is gone/)
  })

  it('saves devices and actions in the project file', () => {
    const project = { ...createProject('x'), devices: [pca(20)], actions: [action] }
    expect(parseProject(serializeProject(project))).toEqual(project)
  })
})
