import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog } from '../../src/backend'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'
import { deviceList, isLiveRead, livePinRead } from '../../src/app/devices/devices'
import { batchValues, pinMask, trimSamples } from '../../src/app/devices/livePins'
import { emptyProject } from '../../src/app/project/projectFile'

const catalog = bundledServiceCatalog()
const board = bundledBoard()
const devices = deviceList(emptyProject(), catalog, board)
const of = (type: string) => devices.find((d) => d.type?.id === type)!

describe('which read follows a pin live', () => {
  it('is the voltage of an analogue pin and the level of an input or an output; nothing for a pin not set up', () => {
    expect(livePinRead(catalog, undefined, of('ads7128'), ['adc', 'unconfigured'])).toBe('voltage')
    expect(livePinRead(catalog, undefined, of('esp32s3_gpio'), ['input'])).toBe('level')
    expect(livePinRead(catalog, undefined, of('esp32s3_gpio'), ['output'])).toBe('level')
    expect(livePinRead(catalog, undefined, of('esp32s3_gpio'), ['unconfigured'])).toBeUndefined()
    expect(livePinRead(catalog, undefined, of('esp32s3_gpio'), ['pwm'])).toBeUndefined()
  })

  it('is none the device does not answer', () => {
    expect(livePinRead(catalog, new Map(), of('ads7128'), ['adc'])).toBeUndefined()
  })

  it('stands for those reads in the panel, not for their writes', () => {
    expect(isLiveRead('IO', 'LEVEL', 'read')).toBe(true)
    expect(isLiveRead('IO', 'LEVEL', 'write')).toBe(false)
    expect(isLiveRead('IO', 'LOCKED', 'read')).toBe(false)
  })
})

describe('the pins of a device in one read', () => {
  it('asks for the pins followed as a mask, up to pin 63', () => {
    expect(pinMask([0, 2, 47])).toBe((1n << 47n) | 5n)
    expect(pinMask([63])).toBe(1n << 63n)
    expect(pinMask([])).toBe(0n)
  })

  it('gives each pin its level from the masks; a pin not read has none', () => {
    const values = batchValues('level', { read: 0b0111, high: 0b0101 }, [0, 1, 2, 3])
    expect([...values]).toEqual([[0, 1], [1, 0], [2, 1], [3, undefined]])
    expect(batchValues('level', { read: 1n << 60n, high: 1n << 60n }, [60]).get(60)).toBe(1)
  })

  it('gives each pin its voltage from the rows; a pin with no row, and every pin of a failed read, has none', () => {
    const values = batchValues('voltage', [{ pin: 1, voltage_mV: 1200 }, { pin: 3, voltage_mV: -5 }], [1, 2, 3])
    expect([...values]).toEqual([[1, 1200], [2, undefined], [3, -5]])
    expect([...batchValues('voltage', undefined, [1])]).toEqual([[1, undefined]])
  })
})

describe('a pin\'s past', () => {
  const at = (...times: number[]) => times.map((t) => ({ at: t, value: t }))

  it('keeps the window and the one value before it, which holds as it starts', () => {
    expect(trimSamples(at(0, 10, 20, 30), 45, 20).map((s) => s.at)).toEqual([20, 30])
    expect(trimSamples(at(0, 10, 20, 30), 40, 20).map((s) => s.at)).toEqual([10, 20, 30])
    expect(trimSamples(at(0, 10), 100, 20).map((s) => s.at)).toEqual([10])
    expect(trimSamples(at(90), 100, 20).map((s) => s.at)).toEqual([90])
    expect(trimSamples([], 100, 20)).toEqual([])
  })
})
