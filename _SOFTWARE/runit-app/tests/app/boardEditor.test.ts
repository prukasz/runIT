import { describe, expect, it } from 'vitest'
import { bundledLinkDefinitions, bundledServiceCatalog } from '../../src/backend'
import { boardProfileFrames } from '../../src/app/devices/boardProfile'
import { boardFromProfile, bootSteps, profileFromBoard, withBoard } from '../../src/app/devices/boardEditor'
import { parseProject, serializeProject } from '../../src/app/project/projectFile'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'
import type { BoardKind } from '../../src/app/devices/boardDescriptor'

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

  it('are devices of the bundled board, ports 0 and 1, each on its pins', () => {
    const buses = profile.devices.filter((d) => d.type === 'esp32s3_i2c')
    expect(buses.map((d) => d.config?.port)).toEqual([0, 1])
    expect(buses.map((d) => `${(d.config?.sda as { pin: number }).pin}/${(d.config?.scl as { pin: number }).pin}`)).toEqual(['15/16', '40/41'])
    expect(buses.every((d) => d.config?.pullup === 1)).toBe(true)
  })

  it('come before the chips on them: the board creates in that order', () => {
    const board = boardFromProfile(profile, base, catalog)
    const at = (id: number) => board.devices.findIndex((d) => d.id === id)
    const chips = board.devices.filter((d) => d.i2c)
    expect(chips.length).toBeGreaterThan(0)
    for (const chip of chips) expect(at(chip.i2c!.bus)).toBeLessThan(at(chip.id))
    // Even from a profile that lists a chip first.
    const flipped = { ...profile, devices: [...profile.devices].reverse() }
    const again = boardFromProfile(flipped, base, catalog)
    for (const chip of again.devices.filter((d) => d.i2c)) expect(again.devices.findIndex((d) => d.id === chip.i2c!.bus)).toBeLessThan(again.devices.findIndex((d) => d.id === chip.id))
  })

  it('reserve no pins of their own: the pins of a bus are those of its device', () => {
    expect(boardFromProfile(profile, base, catalog).reservedPins.some((r) => r.label.startsWith('I2C'))).toBe(false)
  })
})

describe('a profile kept without reserved pins', () => {
  it("has the bundled board's", () => {
    const base = bundledBoard('pcb')
    const { reservedPins: _, ...old } = profileFromBoard(base, catalog)
    expect(withBoard(old, base).reservedPins).toEqual(base.reservedPins)
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
