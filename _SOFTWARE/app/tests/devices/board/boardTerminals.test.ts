import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { connectionProblem } from '../../../src/domain/hardware'
import { BOARD_NETS, BOARD_TERMINALS } from '../../../src/devices/board/boardTerminals'
import { devicePinSpecs } from '../../../src/devices/board/pinSpecs'
import svg from '../../../src/devices/board/runit-board.svg?raw'

const pca = runitDeviceCatalog().type('device_pca9685')!

describe('board terminals', () => {
  it('every terminal is a group of the board art, so a wire has something to attach to', () => {
    const ids = new Set([...svg.matchAll(/id="([^"]+)"/g)].map((match) => match[1]))
    expect([...BOARD_TERMINALS.keys()].filter((id) => !ids.has(id))).toEqual([])
  })

  it('every named net a terminal names is described', () => {
    const named = new Set([...BOARD_TERMINALS.values()].map((spec) => spec.net).filter(Boolean))
    expect([...named].filter((name) => !BOARD_NETS[name as string])).toEqual([])
  })

  it('has the terminals the concept needs: power, ground, I2C, GPIO, analog inputs, servo headers', () => {
    for (const id of ['t3v3-3v3', 'i2c-sda', 'i2c-scl', 'gpio-io46', 'ain0', 'pwm0-pwm', 'pwm0-reg2', 'pwrout6-vs', 'xt60-plus']) expect(BOARD_TERMINALS.has(id)).toBe(true)
    expect(BOARD_TERMINALS.get('gpio-io46')?.sysPin).toEqual({ device: 0, pin: 46 })
    expect(BOARD_TERMINALS.get('i2c-sda')?.sysPin).toEqual({ device: 0, pin: 40 })
  })
})

describe('device pin specs', () => {
  it('a chip brings out every csv pin with its electrical meaning', () => {
    const pins = devicePinSpecs(pca, 'chip')
    expect(pins.size).toBe(28)
    expect(pins.get('VDD')).toMatchObject({ kind: 'supply_in', mV_min: 2300, mV_max: 5500 })
    expect(pins.get('SDA')).toMatchObject({ kind: 'bus', signal: 'i2c-sda' })
    expect(pins.get('OE')?.cfgField).toBe('oe_pin')
  })

  it('a pin limit the user sets replaces the csv limit for that pin only', () => {
    const pins = devicePinSpecs(pca, 'chip', { SDA: { mV_max: 20000 } })
    expect(pins.get('SDA')?.mV_max).toBe(20000)
    expect(pins.get('SCL')?.mV_max).toBe(5500)
  })

  it('a breakout leaves out the pin it does not bring out', () => {
    expect(devicePinSpecs(pca, 'breakout').has('EXTCLK')).toBe(false)
    expect(devicePinSpecs(pca, 'breakout').size).toBe(27)
  })

  it('a PCA9685 wires to the board: SDA and SCL to the I2C terminals, VDD to 3V3, VSS to ground', () => {
    const pins = devicePinSpecs(pca, 'chip')
    const board = (id: string) => BOARD_TERMINALS.get(id)!
    expect(connectionProblem(pins.get('SDA')!, board('i2c-sda'))).toBeUndefined()
    expect(connectionProblem(pins.get('SCL')!, board('i2c-scl'))).toBeUndefined()
    expect(connectionProblem(pins.get('SDA')!, board('i2c-scl'))).toBeDefined()
    expect(connectionProblem(pins.get('VDD')!, board('t3v3-3v3'))).toBeUndefined()
    expect(connectionProblem(pins.get('VSS')!, board('t3v3-gnd'))).toBeUndefined()
    expect(connectionProblem(pins.get('OE')!, board('gpio-io46'))).toBeUndefined()
    // Producer / consumer: IO3 (either) feeds AIN0 (consumer); a PWM channel (producer) feeds an input; two producers do not link.
    expect(connectionProblem(board('gpio-io3'), board('ain0'))).toBeUndefined()
    expect(connectionProblem(board('pwm0-pwm'), board('ain0'))).toBeUndefined()
    expect(connectionProblem(board('pwm0-pwm'), board('pwm1-pwm'))).toContain('both producers')
    expect(connectionProblem(pins.get('LED0')!, pins.get('LED1')!)).toContain('both producers')
    expect(connectionProblem(pins.get('LED0')!, board('gpio-io3'))).toBeUndefined()
  })
})
