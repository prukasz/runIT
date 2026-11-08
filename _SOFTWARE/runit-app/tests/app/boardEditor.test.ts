import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledLinkDefinitions, bundledServiceCatalog } from '../../src/backend'
import type { BoardKind } from '../../src/backend'
import { boardProfileFrames } from '../../src/app/devices/boardProfile'
import { boardFromProfile, bootSteps, profileFromBoard, withBoard } from '../../src/app/devices/boardEditor'
import { parseProject, serializeProject } from '../../src/app/project/projectFile'

const defs = bundledLinkDefinitions()
const catalog = bundledServiceCatalog()
const hex = (frames: Uint8Array[]) => frames.map((f) => Buffer.from(f).toString('hex'))

describe.each(['pcb', 'devkit'] as BoardKind[])('the %s profile', (kind) => {
  const base = bundledBoard(kind)
  const profile = profileFromBoard(base, catalog)

  it('has a device for each of the board\'s', () => {
    expect(profile.devices.map((d) => d.id)).toEqual(base.devices.map((d) => d.id))
  })

  it('makes the same record as the bundled board', () => {
    expect(hex(boardProfileFrames(defs, catalog, boardFromProfile(profile, base, catalog)))).toEqual(hex(boardProfileFrames(defs, catalog, base)))
  })

  it('survives the JSON the board keeps', () => {
    const read = parseProject(serializeProject(profile))
    expect(read.ok && read.project.devices).toEqual(profile.devices)
  })
})

describe('a device added to the profile', () => {
  const base = bundledBoard('pcb')
  const profile = profileFromBoard(base, catalog)
  const gpio = base.devices.find((d) => d.descriptor === 'device_esp32s3_gpio')!
  const added = { id: 40, type: 'servo', name: 'Servo', config: { pwm_pin: { device: gpio.id, pin: 8 } }, defaults: [{ kind: 'device' as const, device: 40, contract: 'servo', command: 'target', args: { value_x10: 0 } }] }
  const next = { ...profile, devices: [added, ...profile.devices] }

  it('is created after the device whose pin it uses, with its boot package after it', () => {
    const board = boardFromProfile(next, base, catalog)
    expect(board.devices.findIndex((d) => d.id === 40)).toBeGreaterThan(board.devices.findIndex((d) => d.id === gpio.id))
    expect(board.devices.find((d) => d.id === 40)?.pins).toEqual([{ use: 'pwm_pin', device: gpio.id, pin: 8 }])
    const plain = boardProfileFrames(defs, catalog, board)
    const withBoot = boardProfileFrames(defs, catalog, board, bootSteps(next))
    expect(withBoot.length).toBe(plain.length + 1)
  })
})

describe('the I2C buses of the profile', () => {
  const base = bundledBoard('pcb')
  const profile = profileFromBoard(base, catalog)
  const gpio = base.devices.find((d) => d.descriptor === 'device_esp32s3_gpio')!

  it('are the bundled board\'s to start with', () => {
    expect(profile.i2cBuses?.map((b) => b.bus)).toEqual(base.i2cBuses.map((b) => b.bus))
    expect(boardFromProfile(profile, base, catalog).i2cBuses).toEqual(base.i2cBuses)
  })

  it('a bus added makes a BUS_CREATE frame and reserves its pins', () => {
    const next = { ...profile, i2cBuses: [...(profile.i2cBuses ?? []), { bus: 2, name: 'Bus 2', sda: { device: gpio.id, pin: 30 }, scl: { device: gpio.id, pin: 31 }, pullup: true }] }
    const board = boardFromProfile(next, base, catalog)
    expect(boardProfileFrames(defs, catalog, board).length).toBe(boardProfileFrames(defs, catalog, base).length + 1)
    expect(board.reservedPins.filter((r) => r.label.startsWith('I2C 2'))).toEqual([{ device: gpio.id, pin: 30, label: 'I2C 2 SDA' }, { device: gpio.id, pin: 31, label: 'I2C 2 SCL' }])
  })

  it('a bus removed takes its frame and its reserved pins', () => {
    const next = { ...profile, i2cBuses: (profile.i2cBuses ?? []).filter((b) => b.bus !== 0) }
    const board = boardFromProfile(next, base, catalog)
    expect(boardProfileFrames(defs, catalog, board).length).toBe(boardProfileFrames(defs, catalog, base).length - 1)
    expect(board.reservedPins.some((r) => r.label.startsWith('I2C 0'))).toBe(false)
  })
})

describe('the record\'s order', () => {
  it('creates every I2C bus before any device', () => {
    const frames = boardProfileFrames(defs, catalog, bundledBoard('pcb'))
    const lastBus = frames.map((f) => f[0]).lastIndexOf(defs.keys.i2c)
    const firstDevice = frames.findIndex((f) => f[0] === defs.keys.device)
    expect(lastBus).toBeGreaterThanOrEqual(0)
    expect(lastBus).toBeLessThan(firstDevice)
  })
})

describe('the UART ports of the profile', () => {
  const base = bundledBoard('pcb')
  const gpio = base.devices.find((d) => d.descriptor === 'device_esp32s3_gpio')!
  const profile = { ...profileFromBoard(base, catalog), uartPorts: [{ provider: 5, port: 2, baud: 115200, mode: 0, parity: 0, stop_bits: 1, tx: { device: gpio.id, pin: 30 }, rx: { device: gpio.id, pin: 31 }, rts: null, cts: null }] }
  const board = boardFromProfile(profile, base, catalog)

  it('a port makes a UART CREATE frame before any device', () => {
    const frames = boardProfileFrames(defs, catalog, board)
    const uart = frames.findIndex((f) => f[0] === defs.keys.uart)
    expect(uart).toBeGreaterThanOrEqual(0)
    expect(uart).toBeLessThan(frames.findIndex((f) => f[0] === defs.keys.device))
  })

  it('reserves its pins', () => {
    expect(board.reservedPins.filter((r) => r.label.startsWith('UART'))).toEqual([{ device: gpio.id, pin: 30, label: 'UART 2 TX' }, { device: gpio.id, pin: 31, label: 'UART 2 RX' }])
  })
})

describe('a profile kept without buses', () => {
  it("has the bundled board's", () => {
    const base = bundledBoard('pcb')
    const { i2cBuses: _, ...old } = profileFromBoard(base, catalog)
    expect(withBoard(old, base).i2cBuses).toEqual(base.i2cBuses)
    expect(base.i2cBuses.map((b) => `${b.bus}:${b.sda.pin}/${b.scl.pin}:${b.pullup}`)).toEqual(['0:15/16:true', '1:40/41:true'])
  })
})

describe('the pins the board sets up at start', () => {
  const base = bundledBoard('pcb')
  const profile = profileFromBoard(base, catalog)

  it('are the Board boot of their device, named by their labels, and make the same record', () => {
    const expander = base.pinSetup[0]!
    const device = profile.devices.find((d) => d.id === expander.device)!
    expect(device.defaults?.some((s) => s.kind === 'device' && s.command === 'MODE' && s.args?.pin === expander.pin)).toBe(true)
    expect(profile.pinAliases).toContainEqual({ device: expander.device, pin: expander.pin, name: expander.label })
    const board = boardFromProfile(profile, base, catalog)
    expect(hex(boardProfileFrames(defs, catalog, board, bootSteps(profile)))).toEqual(hex(boardProfileFrames(defs, catalog, base)))
    expect(board.pinSetup).toEqual(base.pinSetup.map(({ description: _, ...p }) => p))
  })

  it("reserved pins and power are the profile's: another board's profile brings its own", () => {
    const none = { ...profile, reservedPins: [], power: { ...base.power, rails: [] } }
    const board = boardFromProfile(none, base, catalog)
    expect(board.reservedPins.every((r) => r.label.startsWith('I2C'))).toBe(true)
    expect(board.power.rails).toEqual([])
  })
})
