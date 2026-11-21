import { profileFromBoard } from '../../src/app/devices/boardEditor'
import { describe, expect, it } from 'vitest'
import { bundledLinkDefinitions, bundledServiceCatalog, frameImage, parseImage, readDeviceFrame } from '../../src/backend'
import { boardProfileFrames, boardProfileJson } from '../../src/app/devices/boardProfile'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'

const defs = bundledLinkDefinitions()
const catalog = bundledServiceCatalog()
const pcb = bundledBoard('pcb')
const hex = (bytes: Uint8Array): string => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')
const frames = boardProfileFrames(defs, catalog, pcb)
const creates = frames.map((f) => readDeviceFrame(catalog, f)).filter((f) => f?.create)

describe('the board\'s profile', () => {
  it('creates every device of the board but the chip\'s own GPIO (the firmware makes it), each as the board\'s own, in board.json\'s order', () => {
    expect(creates.map((c) => c!.device)).toEqual(pcb.devices.filter((d) => d.descriptor !== 'device_esp32s3_gpio').map((d) => d.id))
    expect(creates.every((c) => c!.create!.system)).toBe(true)
  })

  it('a CREATE is what the firmware takes: the TCA6424A on its bus, its pins on the ESP', () => {
    const tca = creates.find((c) => c!.device === 1)!
    expect(tca.create!.type.id).toBe('tca6424a')
    expect(tca.create!.config).toEqual({ i2c_bus: 14, i2c_addr: 0x23, intr_pin: { device: 0, pin: 9 }, rst_pin: { device: 0, pin: 8 } })
  })

  it('every pin of a DRV8962 goes to its place in the list of pins', () => {
    const pins = creates.find((c) => c!.device === 5)!.create!.config!.pin as readonly ({ device: number; pin: number } | null)[]
    expect(pins[0]).toEqual({ device: 0, pin: 21 }) // IN1
    expect(pins[4]).toEqual({ device: 0, pin: 38 }) // EN1
    expect(pins[8]).toEqual({ device: 0, pin: 7 }) // IPROPI1
    expect(pins[12]).toEqual({ device: 1, pin: 9 }) // nSLEEP
    expect(pins[13]).toEqual({ device: 1, pin: 11 }) // nFAULT
    expect(pins[14]).toEqual({ device: 4, pin: 0 }) // VREF
  })

  it('the layout comes first, the buses after the ESP pins and before the chips on them, the expander\'s pins right after it', () => {
    const read = frames.map((f) => readDeviceFrame(catalog, f))
    expect(frames[0]![0]).toBe(catalog.service('POWER_MANAGER').key)
    const tca = read.findIndex((r) => r?.create && r.device === 1)
    const buses = read.flatMap((r, i) => (r?.create?.type.id === 'esp32s3_i2c' ? [i] : []))
    expect(read.some((r) => r?.create && r.device === 0)).toBe(false)
    expect(buses).toHaveLength(2)
    expect(buses.every((i) => i > 0 && i < tca)).toBe(true)
    expect(read[tca + 1]).toMatchObject({ device: 1, side: 'write' })
  })

  it('a bus takes the pins of the ESP as device pins', () => {
    const bus = creates.find((c) => c!.create!.type.id === 'esp32s3_i2c')!
    expect(bus.create!.config).toMatchObject({ port: 0, sda: { device: 0, pin: 15 }, scl: { device: 0, pin: 16 }, pullup: 1 })
  })

  it('fits one record and gives the same bytes each time', () => {
    const image = frameImage(frames)
    expect(image.length).toBeLessThanOrEqual(4096)
    expect(parseImage(image)).toHaveLength(frames.length)
    expect(hex(frameImage(boardProfileFrames(defs, catalog, pcb)))).toBe(hex(image))
    const profile = profileFromBoard(pcb, catalog)
    expect(hex(boardProfileJson(profile))).toBe(hex(boardProfileJson(profileFromBoard(bundledBoard('pcb'), catalog))))
  })

  it('a devkit: the ESP pins and its two bus devices, no rails, no monitor', () => {
    const devkit = bundledBoard('devkit')
    const own = boardProfileFrames(defs, catalog, devkit)
    const made = own.map((f) => readDeviceFrame(catalog, f)).filter((r) => r?.create).map((r) => r!.device)
    expect(devkit.devices.filter((d) => d.descriptor === 'device_esp32s3_i2c').map((d) => made.includes(d.id))).toEqual([true, true])
    expect(made).not.toContain(0) // its GPIO is the chip's: the firmware makes it
    expect(own.filter((f) => f[0] === catalog.service('POWER_MANAGER').key)).toHaveLength(1)
  })
})
