import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledLinkDefinitions, bundledServiceCatalog, frameImage, parseImage, readDeviceFrame } from '../../src/backend'
import { boardProfileFrames, boardProfileJson } from '../../src/app/devices/boardProfile'

const defs = bundledLinkDefinitions()
const catalog = bundledServiceCatalog()
const pcb = bundledBoard('pcb')
const hex = (bytes: Uint8Array): string => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')
const frames = boardProfileFrames(defs, catalog, pcb)
const creates = frames.map((f) => readDeviceFrame(defs, catalog, f)).filter((f) => f?.create)

describe('the board\'s profile', () => {
  it('creates every device of the board but the chip\'s own GPIO (the firmware makes it), each as the board\'s own, in board.json\'s order', () => {
    expect(creates.map((c) => c!.device)).toEqual(pcb.devices.filter((d) => d.descriptor !== 'device_esp32s3_gpio').map((d) => d.id))
    expect(creates.every((c) => c!.create!.system)).toBe(true)
  })

  it('a CREATE is what the firmware takes: the TCA6424A byte by byte', () => {
    const tca = frames.find((f) => f[0] === defs.keys.device && f[2] === 1 && f[4] === 0 && f[5] === 0)!
    // [key][write][id u16][op u16][type u16][flags][bus][addr][intr: device u16, pin][rst: device u16, pin]
    const type = catalog.devices.get('tca6424a')!.typeId!
    expect(hex(tca)).toBe(hex(Uint8Array.of(defs.keys.device!, defs.dir.write, 1, 0, 0, 0, type & 0xff, type >> 8, 1, 0, 0x23, 0, 0, 9, 0, 0, 8)))
  })

  it('every pin of a DRV8962 goes to its place in the list of pins', () => {
    const drv = frames.find((f) => f[0] === defs.keys.device && f[2] === 5 && f[4] === 0 && f[5] === 0)!
    const pins = drv.subarray(9 + 1, 9 + 1 + 45)
    const at = (slot: number) => [pins[slot * 3]! | (pins[slot * 3 + 1]! << 8), pins[slot * 3 + 2]]
    expect(at(0)).toEqual([0, 21]) // IN1
    expect(at(4)).toEqual([0, 38]) // EN1
    expect(at(8)).toEqual([0, 7]) // IPROPI1
    expect(at(12)).toEqual([1, 9]) // nSLEEP
    expect(at(13)).toEqual([1, 11]) // nFAULT
    expect(at(14)).toEqual([4, 0]) // VREF
  })

  it('the layout comes first, the buses after the ESP pins and before the chips on them, the expander\'s pins right after it', () => {
    const op = (f: Uint8Array) => (f[0] === defs.keys.device ? `device ${f[2]} ${f[4]}/${f[5]}` : `${f[0]}/${f[2]}`)
    const order = frames.map(op)
    expect(frames[0]![0]).toBe(defs.keys.power)
    const tca = order.indexOf('device 1 0/0')
    const buses = frames.flatMap((f, i) => (f[0] === defs.keys.i2c ? [i] : []))
    expect(order).not.toContain('device 0 0/0')
    expect(buses).toHaveLength(2)
    expect(buses.every((i) => i > 0 && i < tca)).toBe(true)
    expect(frames[tca + 1]![2]).toBe(1)
    expect(frames[tca + 1]![0]).toBe(defs.keys.device)
  })

  it('a bus takes the pins of the ESP as device pins', () => {
    const bus = frames.find((f) => f[0] === defs.keys.i2c)!
    expect(Array.from(bus.subarray(3))).toEqual([0, 0, 0, 15, 0, 0, 16, 1])
  })

  it('fits one record and gives the same bytes each time', () => {
    const image = frameImage(frames)
    expect(image.length).toBeLessThanOrEqual(4096)
    expect(parseImage(image)).toHaveLength(frames.length)
    expect(hex(frameImage(boardProfileFrames(defs, catalog, pcb)))).toBe(hex(image))
    expect(hex(boardProfileJson(pcb))).toBe(hex(boardProfileJson(bundledBoard('pcb'))))
  })

  it('a devkit: the ESP pins and its buses, no rails, no monitor', () => {
    const devkit = bundledBoard('devkit')
    const own = boardProfileFrames(defs, catalog, devkit)
    expect(own.filter((f) => f[0] === defs.keys.device && f[4] === 0 && f[5] === 0)).toHaveLength(0) // its one device is the chip's GPIO
    expect(own.filter((f) => f[0] === defs.keys.i2c)).toHaveLength(devkit.i2cBuses.length)
    expect(own.filter((f) => f[0] === defs.keys.power)).toHaveLength(1)
  })
})
