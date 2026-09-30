import { describe, expect, it } from 'vitest'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from '../../../src/domain/descriptors'
import { boardDeviceRef, createProject, parseProject, serializeProject } from '../../../src/domain/project'
import type { ActionStep, ProjectAction, ProjectDevice } from '../../../src/domain/project'
import { buildStoredCode, decodeStoredCode } from '../../../src/domain/storedCode'
import { boardDefaultSettings, runitSettingsIds } from '../../../src/domain/upload'
import { actionRecordSteps, allDevices, buildAction, checkDevices, checkPwm, frequencyWarnings, checkSetup, clampErrorActions, decodeErrorActions, encodeErrorActions, reachableErrorLevels, contractFrame, defaultInstall, deviceInstallSteps, deviceSetupSteps, findContract, installFrame, modesOf, nextDeviceId, pinKey, pinsOf, pinUsers, resolveDevice, withDeviceAliases } from '../../../src/domain/devices'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitDeviceCatalog()
const commands = runitCommandCatalog()

const pca = (deviceId: number, extra: Record<string, number> = {}): ProjectDevice => {
  const type = catalog.type('device_pca9685')!
  return { id: `pca-${deviceId}`, deviceId, type: type.id, name: `pwm ${deviceId}`, tags: ['servo'], install: { ...defaultInstall(type, catalog.i2cBuses.user), i2c_addr: 0x41, ...extra } }
}

describe('device catalog', () => {
  it('marks ADC as the ADS7128 device default without adding setup frames', () => {
    const adc = resolveDevice(catalog, [], boardDeviceRef(2))!
    const mode = findContract(catalog, adc, 'packet_sys_io_set_mode_t')!.parameters.find((parameter) => parameter.name === 'mode')!
    expect(mode.choices?.map((choice) => choice.value)).toEqual([7])
    expect(mode.defaultValue).toBe(7)
    expect(deviceSetupSteps(catalog, [], []).steps).toEqual([])
  })

  it('defaults ADS7128 ALERT to pull-up and exposes only input modes supported by the chosen provider', () => {
    const type = catalog.type('device_ads_7128')!
    expect(type.installChoices.get('intr_pin_mode')?.map((choice) => choice.value)).toEqual([0, 1])
    expect(defaultInstall(type, catalog.i2cBuses.user).intr_pin_mode).toBe(1)
    expect(modesOf(catalog.board.find((device) => device.deviceId === 0)?.type).map((choice) => choice.value)).toContain(1)
    expect(modesOf(catalog.board.find((device) => device.deviceId === 1)?.type).map((choice) => choice.value)).not.toContain(1)
    const device: ProjectDevice = { id: 'adc', deviceId: 20, type: type.id, name: 'ADC', tags: [], install: { ...defaultInstall(type, catalog.i2cBuses.user), intr_pin_device_id: 1, intr_pin_pin: 0 } }
    expect(checkDevices(catalog, [device]).some((diagnostic) => diagnostic.message.includes('mode 1 is not available'))).toBe(true)
    expect(checkDevices(catalog, [{ ...device, install: { ...device.install, intr_pin_mode: 0 } }]).some((diagnostic) => diagnostic.message.includes('mode 0 is not available'))).toBe(false)
  })

  it('publishes selectable I2C addresses and rejects a loaded address outside the list', () => {
    const expected: Record<string, number[]> = {
      device_ads_7128: [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17],
      device_ap33772s: [0x52],
      device_dac53202: [0x48, 0x49, 0x4a, 0x4b],
      device_ina3221: [0x40, 0x41, 0x42, 0x43],
      device_tca6424a: [0x22, 0x23],
      device_tps55289: [0x74, 0x75],
    }
    for (const [id, addresses] of Object.entries(expected)) {
      const type = catalog.type(id)!
      expect(type.installChoices.get('i2c_addr')?.map((choice) => choice.value)).toEqual(addresses)
      expect(addresses).toContain(defaultInstall(type, catalog.i2cBuses.user).i2c_addr)
    }
    const pcaAddresses = catalog.type('device_pca9685')!.installChoices.get('i2c_addr')!.map((choice) => choice.value)
    expect(pcaAddresses).toContain(0x40)
    expect(pcaAddresses).not.toContain(0x70)
    expect(pcaAddresses).not.toContain(0x7f)
    expect(checkDevices(catalog, [pca(20, { i2c_addr: 0 })])).toEqual(expect.arrayContaining([expect.objectContaining({ message: expect.stringContaining('I2C address 0x0 is not available') })]))
  })

  it('uses project aliases for display without changing device identities or install frames', () => {
    const device = pca(20)
    const named = withDeviceAliases(catalog, { 'board:0': 'Front switches', [device.id]: 'Arm servos' })
    expect(allDevices(named, [device])).toEqual(expect.arrayContaining([
      expect.objectContaining({ ref: 'board:0', deviceId: 0, name: 'Front switches' }),
      expect.objectContaining({ ref: device.id, deviceId: 20, name: 'Arm servos' }),
    ]))
    const original = deviceInstallSteps(catalog, [device])
    const aliased = deviceInstallSteps(named, [device])
    expect(aliased.steps[0]?.label).toContain('Arm servos')
    expect(aliased.steps.map((step) => step.frame)).toEqual(original.steps.map((step) => step.frame))
    expect(resolveDevice(catalog, [device], device.id)?.name).toBe(device.name)
    expect(catalog.board.find((entry) => entry.deviceId === 0)?.name).not.toBe('Front switches')
    expect(resolveDevice(withDeviceAliases(catalog), [device], device.id)?.name).toBe(device.name)
  })

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
    expect(type.pinGroups).toEqual([expect.objectContaining({ key: 'oe_pin', use: 'Output enable', label: 'Output enable pin', note: 'Active-low.', pinField: 'oe_pin_pin', deviceField: 'oe_pin_device_id', modeField: 'oe_pin_mode', sentinel: 255 })])
  })

  it('names pins from the header alias, board links included, and carries field notes', () => {
    const ads = catalog.type('device_ads_7128')!
    expect(ads.pinGroups).toEqual([expect.objectContaining({ key: 'intr_pin', use: 'ALERT', label: 'ALERT pin', note: 'Open-drain, active-low.' })])
    expect(catalog.board.find((device) => device.deviceId === 2)?.pins).toEqual([expect.objectContaining({ use: 'intr_pin', label: 'ALERT' })])
    expect(catalog.board.find((device) => device.deviceId === 1)?.pins.map((pin) => pin.label)).toEqual(['Interrupt', 'Reset'])
    expect(pinUsers(catalog, []).get(pinKey(0, 42))).toEqual([expect.objectContaining({ ownerName: 'ADS7128', use: 'ALERT' })])
    const gpio = catalog.type('device_gpio_esp')!
    const parameter = (contract: string, name: string) => gpio.contracts.find((entry) => entry.id === contract)?.parameters.find((entry) => entry.name === name)
    expect(parameter('packet_sys_io_set_pwm_duty_t', 'duty')?.note).toBe('4096 = always on.')
    expect(parameter('packet_sys_io_configure_intr_t', 'debounce')?.note).toBe('1 = ignore switch bounce')
    expect(parameter('packet_sys_io_reset_t', 'pin')?.note).toBeUndefined()
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
    // sys_device.c: Disabled ignores everything; from Low up, importance admits progressively lower severities.
    expect([0, 1, 2, 3, 4].map(reachableErrorLevels)).toEqual([[], [4], [3, 4], [2, 3, 4], [1, 2, 3, 4]])
    // A device starts at Low (the packet's default), so a new form doesn't switch errors off.
    const handling = findContract(catalog, resolveDevice(catalog, [], boardDeviceRef(13))!, 'packet_sys_device_set_error_handling_t')!
    expect(handling.parameters.find((parameter) => parameter.name === 'importance')?.defaultValue).toBe(1)
    const disabled = checkSetup(catalog, [], [{ id: 'e1', device: boardDeviceRef(13), contract: 'packet_sys_device_set_error_handling_t', values: { importance: 0, actions: [0, 0, 0, 0, 0] } }])
    expect(disabled).toEqual([expect.objectContaining({ severity: 'warning', message: expect.stringMatching(/Disabled — every error of this device is ignored, Critical included/) })])
    expect(clampErrorActions(encodeErrorActions([{ scope: 'dynamic', id: 1 }, { scope: 'dynamic', id: 2 }, { scope: 'static', id: 3 }, { scope: 'static', id: 4 }]), 1)).toEqual([0, 0, 0, 0, 4])
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
