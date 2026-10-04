import { describe, expect, it } from 'vitest'
import { newBoard } from '../../../src/domain/board'
import type { BoardTemplate } from '../../../src/domain/board'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { defaultInstall } from '../../../src/domain/devices'
import type { Wire } from '../../../src/domain/hardware'
import type { ProjectDevice } from '../../../src/domain/project'
import { BOARD_TERMINALS } from '../../../src/devices/board/boardTerminals'
import { RUNIT_MODEL, buildBoardModel } from '../../../src/devices/board/boardModel'
import { sysPinProviders, sysPinSpec } from '../../../src/devices/board/sysPins'
import { evaluateWiring } from '../../../src/devices/board/wiringReport'

const catalog = runitDeviceCatalog()

const template: BoardTemplate = {
  ...newBoard('lab', 'Lab board'),
  connectors: [
    {
      id: 'c1',
      type: 'header1x4',
      x: 20,
      y: 12,
      rotation: 0,
      mirror: false,
      pins: [
        { label: '3V3', bind: { type: 'net', net: '3V3' } },
        { label: 'GND', bind: { type: 'net', net: 'GND' } },
        { label: 'IO46', bind: { type: 'sys', device: 0, pin: 46 } },
        { label: 'AIN0', bind: { type: 'sys', device: 2, pin: 0, overrides: { mV_max: 20000 } } },
      ],
    },
    {
      id: 'c2',
      type: 'screw2',
      x: 50,
      y: 12,
      rotation: 0,
      mirror: false,
      pins: [{ label: 'SDA', bind: { type: 'signal', signal: 'i2c-sda', sys: { device: 0, pin: 40 } } }, { label: 'SCL', bind: { type: 'signal', signal: 'i2c-scl' } }],
    },
  ],
}

describe('the built-in board model', () => {
  it('is the runIT board: its art, its size and its terminal table', () => {
    expect(RUNIT_MODEL.builtin).toBe(true)
    expect(RUNIT_MODEL.terminals).toBe(BOARD_TERMINALS)
    expect(RUNIT_MODEL.widthMm).toBeGreaterThan(20)
    expect(RUNIT_MODEL.heightMm).toBeGreaterThan(RUNIT_MODEL.widthMm)
  })
})

describe('a board from a template', () => {
  const model = buildBoardModel(template, catalog)

  it('has the template size in millimetres and art at 20 units to the millimetre', () => {
    expect(model.builtin).toBe(false)
    expect(model.widthMm).toBe(80)
    expect(model.heightMm).toBe(60)
    expect(model.art.width).toBe(1600)
    expect(model.art.height).toBe(1200)
    expect(model.art.markup).toContain('<g id="c1-3">')
  })

  it('has a terminal per bound pin, with the meaning its binding gives it', () => {
    expect([...model.terminals.keys()]).toEqual(['c1-1', 'c1-2', 'c1-3', 'c1-4', 'c2-1', 'c2-2'])
    expect(model.terminals.get('c1-1')).toMatchObject({ kind: 'supply_out', net: '3V3', label: '3V3' })
    expect(model.terminals.get('c1-2')).toMatchObject({ kind: 'gnd' })
    expect(model.terminals.get('c1-3')).toMatchObject({ kind: 'io', dir: 'inout', sysPin: { device: 0, pin: 46 } })
    expect(model.terminals.get('c1-4')).toMatchObject({ kind: 'io', dir: 'in', modes: ['ADC'], mV_max: 20000, sysPin: { device: 2, pin: 0 } })
    expect(model.terminals.get('c2-1')).toMatchObject({ kind: 'bus', signal: 'i2c-sda', dir: 'inout', sysPin: { device: 0, pin: 40 } })
    expect(model.terminals.get('c2-2')).toMatchObject({ kind: 'bus', signal: 'i2c-scl', dir: 'out' })
  })

  it('writes what each pin is into its tooltip', () => {
    expect(model.art.markup).toContain('<title>IO46 · GPIO_ESP')
    expect(model.art.markup).toContain('<title>SDA · I2C SDA</title>')
    expect(model.art.markup).toContain('<title>3V3 · 3.3 V</title>')
  })
})

describe('installed sys pins', () => {
  it('the role of a pin comes from the catalog: what its provider can do with a pin, not a table of devices', () => {
    // The ESP can set, read and PWM a pin: either. The ADS7128 can only read a voltage: a consumer.
    expect(sysPinSpec(catalog, { device: 0, pin: 3 })).toMatchObject({ kind: 'io', dir: 'inout', modes: ['BINARY', 'PWM', 'ADC'] })
    expect(sysPinSpec(catalog, { device: 2, pin: 5 })).toMatchObject({ dir: 'in', modes: ['ADC'] })
    expect(sysPinSpec(catalog, { device: 1, pin: 10 })).toMatchObject({ dir: 'inout', modes: ['BINARY'] })
  })

  it('a provider with a pins csv gives each channel the direction and modes the csv names', () => {
    expect(sysPinSpec(catalog, { device: 3, pin: 7 })).toMatchObject({ dir: 'out', modes: ['BINARY', 'PWM'], label: 'PCA9685 LED7 (7)' })
  })

  it('a provider the catalog cannot place is either, and the user can override it', () => {
    // The DAC writes and reads a voltage, so it is not told apart from a pin that can do both.
    expect(sysPinSpec(catalog, { device: 4, pin: 1 })).toMatchObject({ dir: 'inout', modes: ['DAC'] })
  })

  it('limits come from the data file by device type: the ESP and the TCA tolerate 3.6 V, the ADS7128 states none', () => {
    expect(sysPinSpec(catalog, { device: 0, pin: 3 })?.mV_max).toBe(3600)
    expect(sysPinSpec(catalog, { device: 1, pin: 3 })?.mV_max).toBe(3600)
    expect(sysPinSpec(catalog, { device: 2, pin: 0 })?.mV_max).toBeUndefined()
  })

  it('a pin the board does not have is not a pin', () => {
    expect(sysPinSpec(catalog, { device: 2, pin: 8 })).toBeUndefined()
    expect(sysPinSpec(catalog, { device: 99, pin: 0 })).toBeUndefined()
  })

  it('lists the providers with their pins, and flags the ones the board already uses', () => {
    const providers = sysPinProviders(catalog, [])
    expect(providers.map((provider) => provider.name)).toEqual(expect.arrayContaining(['GPIO_ESP', 'TCA6424A', 'ADS7128', 'PCA9685', 'DAC53202']))
    const gpio = providers.find((provider) => provider.device === 0)!
    expect(gpio.pins).toHaveLength(45)
    expect(gpio.pins.find((pin) => pin.pin === 40)?.takenBy).toContain('I2C 1 SDA')
    expect(gpio.pins.find((pin) => pin.pin === 46)?.takenBy).toBeUndefined()
    expect(providers.find((provider) => provider.device === 1)!.pins.find((pin) => pin.pin === 22)?.takenBy).toContain('Status LEDs')
  })
})

describe('wiring to a custom board', () => {
  const pca = catalog.type('device_pca9685')!
  const device: ProjectDevice = { id: 'd1', deviceId: 20, type: pca.id, name: 'arm', tags: [], install: defaultInstall(pca, catalog.i2cBuses.user) }
  const wire = (id: string, pin: string, terminal: string): Wire => ({ id, a: { device: 'd1', pin }, b: { device: 'board', pin: terminal } })
  const state = (wires: Wire[], board?: BoardTemplate) => ({ devices: [device], catalog, boardLayout: { d1: { x: 0, y: 0, rotation: 0 as const, mirror: false, variant: 'chip' } }, wires, ...(board ? { board } : {}) })

  it('a device pin wires to a connector pin like to a built-in terminal', () => {
    const report = evaluateWiring(state([wire('w1', 'VDD', 'c1-1'), wire('w2', 'VSS', 'c1-2'), wire('w3', 'OE', 'c1-3'), wire('w4', 'SDA', 'c2-1'), wire('w5', 'SCL', 'c2-2')], template))
    expect(report.issues.filter((issue) => issue.severity === 'error')).toEqual([])
    expect(report.links[0]).toMatchObject({ device: 'd1', field: 'oe_pin', sysPin: { device: 0, pin: 46 } })
  })

  it('wires made for the built-in board are reported as dangling on a custom one', () => {
    const report = evaluateWiring(state([wire('w1', 'VDD', 't3v3-3v3')], template))
    expect(report.issues.map((issue) => issue.code)).toContain('dangling')
  })

  it('the same wires are fine on the built-in board', () => {
    expect(evaluateWiring(state([wire('w1', 'VDD', 't3v3-3v3')])).issues.map((issue) => issue.code)).not.toContain('dangling')
  })
})

describe('board data', () => {
  it('the bus signals and which nets can be fed from outside are data, not code', async () => {
    const { BOARD_NETS, BOARD_SIGNALS } = await import('../../../src/devices/board/boardTerminals')
    expect(BOARD_SIGNALS.map((signal) => signal.id)).toEqual(expect.arrayContaining(['i2c-sda', 'i2c-scl', 'uart-tx', 'uart-rx', 'spi-mosi', 'spi-miso', 'spi-sck', 'spi-cs']))
    expect(BOARD_SIGNALS.find((signal) => signal.id === 'i2c-scl')?.dir).toBe('out')
    expect(Object.entries(BOARD_NETS).filter(([, net]) => net.input).map(([name]) => name)).toEqual(['VS'])
  })
})

describe('pin aliases', () => {
  it('the sys pin picker names a pin by its alias, with the number beside it, and by "Pin n" otherwise', async () => {
    const { withDeviceAliases } = await import('../../../src/domain/devices')
    const aliased = withDeviceAliases(catalog, {}, { 'board:0': { '41': 'Sensor SCL', '32': 'Buzzer' } })
    const gpio = sysPinProviders(aliased, []).find((provider) => provider.device === 0)!
    expect(gpio.pins.find((pin) => pin.pin === 41)?.label).toBe('Sensor SCL (pin 41)')
    expect(gpio.pins.find((pin) => pin.pin === 32)?.label).toBe('Buzzer (pin 32)')
    expect(gpio.pins.find((pin) => pin.pin === 33)?.label).toBe('Pin 33')
    expect(sysPinProviders(catalog, []).find((provider) => provider.device === 0)!.pins.find((pin) => pin.pin === 41)?.label).toBe('Pin 41')
  })

  it('a bound pin is described by its alias too', async () => {
    const { withDeviceAliases } = await import('../../../src/domain/devices')
    const aliased = withDeviceAliases(catalog, {}, { 'board:0': { '46': 'Status LED' } })
    expect(sysPinSpec(aliased, { device: 0, pin: 46 })?.label).toContain('Status LED (pin 46)')
    expect(buildBoardModel({ ...template, connectors: [template.connectors[0]] }, aliased).art.markup).toContain('Status LED (pin 46)')
  })
})
