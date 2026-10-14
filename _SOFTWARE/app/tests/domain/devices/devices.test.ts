import { describe, expect, it } from 'vitest'
import { BOARD_DEVICES, DEVICE_TYPES, deviceProblems, deviceType, devicesFromJson, installEntries, newDevice, nextDeviceId, NO_DEVICE } from '../../../src/domain/devices'

describe('the device catalog, read from the generated descriptors', () => {
  it('has every device type with its guide, and the board devices linked to their types', () => {
    expect(DEVICE_TYPES.map((type) => type.id)).toContain('device_servo')
    expect(deviceType('device_servo')!.guide).toMatch(/^# Servo/)
    expect(BOARD_DEVICES.find((device) => device.id === 3)).toMatchObject({ alias: 'PWM expander', type: { id: 'device_pca9685' } })
    expect(BOARD_DEVICES.every((device) => device.type)).toBe(true)
  })

  it('lays the create struct out as form rows: numbers, pins of other devices, array elements named by their aliases', () => {
    const servo = installEntries(deviceType('device_servo'))
    expect(servo[0]).toMatchObject({ kind: 'pin', key: 'pwm_pin', label: 'PWM pin' })
    expect(servo.find((entry) => entry.key === 'servo_type')).toMatchObject({ kind: 'number', label: 'Servo type' })
    const drv = installEntries(deviceType('device_drv8962'))
    expect(drv.find((entry) => entry.key === 'pin[0]')).toMatchObject({ kind: 'pin', label: 'IN1' })
    expect(drv.find((entry) => entry.key === 'pin[14]')).toMatchObject({ kind: 'pin', label: 'VREF' })
  })
})

describe('the user devices', () => {
  it('a new device takes the next free ID above the board, the defaults, and unwired pins', () => {
    const servo = newDevice('device_servo', [])
    expect(servo.deviceId).toBe(Math.max(...BOARD_DEVICES.map((device) => device.id)) + 1)
    expect(servo.install['pwm_pin.device']).toBe(NO_DEVICE)
    expect(servo.install.servo_type).toBe(0)
    expect(nextDeviceId([servo])).toBe(servo.deviceId + 1)
  })

  it('names what is wrong: a board ID, a shared ID, a required pin left unwired', () => {
    const servo = newDevice('device_servo', [])
    const messages = deviceProblems([{ ...servo, deviceId: 3 }, { ...servo, id: 'other', deviceId: 3 }]).map((problem) => problem.message)
    expect(messages).toContain("Device ID 3 is the board's PWM expander.")
    expect(messages.some((message) => message.includes('also'))).toBe(true)
    expect(messages).toContain('PWM pin must be wired to a pin.')
  })

  it('reads saved devices back, a missing value takes its default', () => {
    const [device] = devicesFromJson([{ id: 'd', type: 'device_servo', deviceId: 20, name: 'Arm', tags: ['x'], install: { servo_type: 1 } }])
    expect(device).toMatchObject({ name: 'Arm', deviceId: 20, tags: ['x'] })
    expect(device!.install.servo_type).toBe(1)
    expect(device!.install['pwm_pin.device']).toBe(NO_DEVICE)
    expect(devicesFromJson('nope')).toEqual([])
  })
})

describe('pin discovery, modes, and usage tracking', () => {
  it('extracts pins provided by device types from their IO contract ops', async () => {
    const { getDevicePins, isOutputMode } = await import('../../../src/domain/devices')
    const esp = getDevicePins(deviceType('device_gpio_esp'))
    expect(esp.length).toBeGreaterThan(20)
    expect(esp[0]).toEqual({ pin: 0, label: 'GPIO 0' })

    const tca = getDevicePins(deviceType('device_tca6424a'))
    expect(tca.length).toBe(24)
    expect(tca[0].pin).toBe(0)

    const pca = getDevicePins(deviceType('device_pca9685'))
    expect(pca.length).toBe(16)
    expect(pca[0]).toEqual({ pin: 0, label: 'PWM CH 0' })

    expect(isOutputMode(3)).toBe(true)
    expect(isOutputMode(4)).toBe(true)
    expect(isOutputMode(5)).toBe(true)
    expect(isOutputMode(0)).toBe(false)
  })

  it('formats pin display labels with aliases when defined', async () => {
    const { pinDisplayLabel } = await import('../../../src/domain/devices')
    const aliases = {
      'board:0': { '8': 'Status LED' },
    }
    expect(pinDisplayLabel(aliases, 'board:0', 8, 'GPIO 8')).toBe('Status LED (pin 8)')
    expect(pinDisplayLabel(aliases, 'board:0', 9, 'GPIO 9')).toBe('GPIO 9 (9)')
    expect(pinDisplayLabel(undefined, 'board:0', 9)).toBe('Pin 9')
  })

  it('tracks pins taken by onboard links and user devices', async () => {
    const { pinUsers, pinKey } = await import('../../../src/domain/devices')
    const servo = newDevice('device_servo', [])
    const wiredServo = {
      ...servo,
      install: {
        ...servo.install,
        'pwm_pin.device': 3, // PCA9685
        'pwm_pin.pin': 2,
      },
    }

    const users = pinUsers([wiredServo])
    // Check fixed link on TCA6424A pin 23 (Status LED)
    const ledUsers = users.get(pinKey(1, 23))
    expect(ledUsers).toBeDefined()
    expect(ledUsers![0].use).toBe('Status LED')

    // Check user device on PCA9685 pin 2
    const servoPinUsers = users.get(pinKey(3, 2))
    expect(servoPinUsers).toBeDefined()
    expect(servoPinUsers!.some((u) => u.ownerRef === wiredServo.id)).toBe(true)
    expect(servoPinUsers!.find((u) => u.ownerRef === wiredServo.id)?.mode).toBe(6) // PWM mode tracked
  })

  it('filters devices and pins by supported capabilities and modes', async () => {
    const { deviceProvidesAnyMode, entryRequiredModes, getDevicePins } = await import('../../../src/domain/devices')

    const esp = deviceType('device_gpio_esp')
    const pca = deviceType('device_pca9685')
    const tca = deviceType('device_tca6424a')
    const dac = deviceType('device_dac53202')

    // PWM capability (mode 6)
    expect(deviceProvidesAnyMode(esp, [6])).toBe(true)
    expect(deviceProvidesAnyMode(pca, [6])).toBe(true)
    expect(deviceProvidesAnyMode(tca, [6])).toBe(false)
    expect(deviceProvidesAnyMode(dac, [6])).toBe(false)

    // ADC capability (mode 7)
    expect(deviceProvidesAnyMode(esp, [7])).toBe(true)
    expect(deviceProvidesAnyMode(pca, [7])).toBe(false)
    expect(deviceProvidesAnyMode(tca, [7])).toBe(false)

    // DAC capability (mode 8)
    expect(deviceProvidesAnyMode(esp, [8])).toBe(false)
    expect(deviceProvidesAnyMode(dac, [8])).toBe(true)

    // Pin filtering by capability
    const espAdcPins = getDevicePins(esp, [7])
    expect(espAdcPins.length).toBe(45)

    const pcaAdcPins = getDevicePins(pca, [7])
    expect(pcaAdcPins.length).toBe(0)

    const pcaPwmPins = getDevicePins(pca, [6])
    expect(pcaPwmPins.length).toBe(16)

    const tcaPwmPins = getDevicePins(tca, [6])
    expect(tcaPwmPins.length).toBe(0)

    const tcaIoPins = getDevicePins(tca, [0])
    expect(tcaIoPins.length).toBe(24)

    // entryRequiredModes for install entries
    const servoEntries = installEntries(deviceType('device_servo'))
    const servoPwm = servoEntries.find((e) => e.key === 'pwm_pin')!
    expect(entryRequiredModes(servoPwm.field)).toEqual([6])

    const drvEntries = installEntries(deviceType('device_drv8962'))
    const in1 = drvEntries.find((e) => e.label === 'IN1')!
    const ipropi1 = drvEntries.find((e) => e.label === 'IPROPI1')!
    const vref = drvEntries.find((e) => e.label === 'VREF')!
    expect(entryRequiredModes(in1.field)).toEqual([6]) // PWM
    expect(entryRequiredModes(ipropi1.field)).toEqual([7]) // ADC
    expect(entryRequiredModes(vref.field)).toEqual([8]) // DAC
  })
})
