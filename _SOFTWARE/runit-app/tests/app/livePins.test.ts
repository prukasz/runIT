import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog } from '../../src/backend'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'
import { deviceList, isLiveRead, livePinRead } from '../../src/app/devices/devices'
import { trimSamples } from '../../src/app/devices/livePins'
import { emptyProject } from '../../src/app/project/projectFile'

const catalog = bundledServiceCatalog()
const board = bundledBoard()
const devices = deviceList(emptyProject(), catalog, board)
const of = (type: string) => devices.find((d) => d.type?.id === type)!

describe('which read follows a pin live', () => {
  it('is the voltage of an analogue pin and the level of an input or an output; nothing for a pin not set up', () => {
    expect(livePinRead(catalog, undefined, of('ads7128'), ['adc', 'unconfigured'])?.command).toBe('VOLTAGE')
    expect(livePinRead(catalog, undefined, of('esp32s3_gpio'), ['input'])?.command).toBe('LEVEL')
    expect(livePinRead(catalog, undefined, of('esp32s3_gpio'), ['output'])?.command).toBe('LEVEL')
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
