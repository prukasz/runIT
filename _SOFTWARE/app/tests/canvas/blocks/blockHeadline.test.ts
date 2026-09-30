import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { blockDeviceLine, blockHeadline, blockHeadlineParts, blockShape, settingText } from '../../../src/canvas/blocks/blockView'

describe('block headline', () => {
  const catalog = runitVmCatalog()
  const headline = (key: string, settings: Record<string, number | string>) => blockHeadline(catalog.block(key), { id: 'b', type: key, settings })

  it('shows the main settings after the title', () => {
    expect(headline('PERIODIC', { period: 100, time_base: 'MS' })).toBe('Every 100 MS')
    expect(headline('TIMER', { mode: 'TON', time_base: 'MS', pt: 300 })).toBe('Timer TON 300 MS')
    expect(headline('FOR', { k_start: 0, k_end: 3, k_step: 1, op: 'ADD', cmp: 'LT' })).toBe('For 0 to 3 (<) step +1')
    expect(headline('LATCH', { mode: 'SET_DOMINANT' })).toBe('Latch Set dominant')
    expect(headline('EDGE', { edge_type: 'RISING' })).toBe('RISING edge')
  })

  it('splits the words of the block from the value it is set to, so the face can draw the value apart', () => {
    const parts = (key: string, settings: Record<string, number | string>) => blockHeadlineParts(catalog.block(key), { id: 'b', type: key, settings })
    expect(parts('PERIODIC', { period: 300, time_base: 'MS' })).toEqual({ lead: 'Every', value: '300 MS' })
    expect(parts('TIMER', { mode: 'TON', time_base: 'MS', pt: 300 })).toEqual({ lead: 'Timer', value: 'TON 300 MS' })
    expect(parts('LATCH', { mode: 'SET_DOMINANT' })).toEqual({ lead: 'Latch Set dominant' })
  })

  it('names the pin an IO block drives, and the device it is on', () => {
    const toggle = { id: 'b', type: 'IO_TOGGLE', settings: { device_id: 1, default_io_num: 22 } }
    expect(blockHeadline(catalog.block('IO_TOGGLE'), toggle)).toBe('Toggle Pin #22')
    expect(blockHeadlineParts(catalog.block('IO_SET_LEVEL'), { id: 'b', type: 'IO_SET_LEVEL', settings: { default_io_num: 5 } })).toEqual({ lead: 'Set Pin Level', value: '#5' })
    expect(blockDeviceLine(catalog.block('IO_TOGGLE'), toggle)).toMatch(/\(#1\)$/)
    expect(blockDeviceLine(catalog.block('EXPR'), { id: 'b', type: 'EXPR' })).toBeUndefined()
  })

  it('names what feeds a setting once its input pin is wired, instead of the unused constant', () => {
    const wiredPeriod = { id: 'b', type: 'PERIODIC', settings: { period: 0, time_base: 'MS' }, inputs: [{ root: 'rate' }] }
    expect(blockHeadline(catalog.block('PERIODIC'), wiredPeriod)).toBe('Every rate MS')
    expect(blockHeadline(catalog.block('PERIODIC'), wiredPeriod, (path) => path.root.toUpperCase())).toBe('Every RATE MS')
    // An empty variable ({ root: '' }) or an unwired pin keeps the constant.
    expect(blockHeadline(catalog.block('PERIODIC'), { ...wiredPeriod, inputs: [{ root: '' }] })).toBe('Every 0 MS')
    expect(blockHeadline(catalog.block('PERIODIC'), { ...wiredPeriod, inputs: [null] })).toBe('Every 0 MS')
    expect(blockHeadline(catalog.block('TIMER'), { id: 'b', type: 'TIMER', settings: { mode: 'TON', time_base: 'S', pt: 5 }, inputs: [null, { root: 'delay' }] })).toBe('Timer TON delay S')
    expect(blockHeadline(catalog.block('FOR'), { id: 'b', type: 'FOR', settings: { k_start: 0, k_end: 3, k_step: 1, op: 'ADD', cmp: 'LT' }, inputs: [null, { root: 'n' }] })).toBe('For 0 to n (<) step +1')
  })

  it('has none for a block without settings worth showing', () => {
    expect(headline('IF', {})).toBeUndefined()
  })
})

describe('block face from the descriptor', () => {
  const catalog = runitVmCatalog()
  const loop = { id: 'l', type: 'FOR', settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10, op: 'ADD', cmp: 'LT' }, body: 2 }

  it('has a detailed view only where there is a hard-coded input to show, or a formula', () => {
    const detailed = (key: string) => catalog.block(key)!.hasDetail
    expect(['FOR', 'TIMER', 'PERIODIC', 'ACTION', 'IO_SET_LEVEL', 'IO_TOGGLE', 'EXPR', 'EXPR_BIT'].filter((key) => !detailed(key))).toEqual([])
    expect(['IF', 'SWITCH', 'SET', 'CLONE', 'EDGE', 'LATCH', 'ON_EVENT'].filter(detailed)).toEqual([])
  })

  it('draws an input that has a constant only in the detailed view, with the constant', () => {
    const loopType = catalog.block('FOR')!
    expect(blockShape(loopType, { id: 'l', type: 'FOR' }).inputs).toEqual([])
    const detailed = blockShape(loopType, { id: 'l', type: 'FOR', view: 'detailed' })
    expect(blockShape(loopType, { id: 'l', type: 'FOR', view: 'simple' }, true).inputs).toEqual([])
    expect(detailed.inputs.map((pin) => [pin.title, pin.overrides])).toEqual([['Start', 'k_start'], ['End', 'k_end'], ['Step', 'k_step']])
    expect(settingText(loopType, loop, 'k_end')).toBe('3')
  })

  it('hides an input that has a constant until it is wired, and ties the constant to its pin', () => {
    const timer = catalog.block('TIMER')!
    const pt = timer.inputs.pins.find((pin) => pin.name === 'pt')!
    expect(pt.overrides).toBe('pt')
    expect(timer.fields.find((field) => field.name === 'pt')?.overriddenBy).toBe(pt.index)
    expect(blockShape(timer, { id: 't', type: 'TIMER' }).inputs.map((pin) => pin.index)).toEqual([0])
    expect(blockShape(timer, { id: 't', type: 'TIMER', inputs: [null, { root: 'delay' }] }).inputs.map((pin) => pin.index)).toEqual([0, 1])
    for (const pin of catalog.block('FOR')!.inputs.pins) expect(pin.hiddenByDefault).toBe(true)
    expect(blockShape(catalog.block('FOR'), { id: 'l', type: 'FOR' }).inputs).toEqual([])
  })

  it('gives the timer one output, elapsed time, and names its ENO Q', () => {
    const timer = catalog.block('TIMER')!
    expect(timer.outputs.pins.map((pin) => pin.name)).toEqual(['et'])
    expect(timer.eno.title).toBe('Q')
  })

  it('shows a wired IO pin by its source, and a dynamic one as such', () => {
    const io = catalog.block('IO_TOGGLE')
    const settings = { device_id: 0, default_io_num: 4 }
    expect(blockHeadline(io, { id: 'i', type: 'IO_TOGGLE', settings, inputs: [{ root: 'sel' }] })).toBe('Toggle Pin sel')
    expect(blockHeadline(io, { id: 'i', type: 'IO_TOGGLE', settings, dynamicInputs: [0] })).toBe('Toggle Pin dynamic pin')
  })
})
