import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../domain/descriptors'
import { blockDeviceLine, blockHeadline, blockHeadlineParts } from './blockView'

describe('block headline', () => {
  const catalog = runitVmCatalog()
  const headline = (key: string, settings: Record<string, number | string>) => blockHeadline(catalog.block(key), { id: 'b', type: key, settings })

  it('shows the main settings after the title', () => {
    expect(headline('PERIODIC', { period: 100, time_base: 'MS' })).toBe('Every 100 MS')
    expect(headline('TIMER', { mode: 'TON', time_base: 'MS', pt: 300 })).toBe('Timer TON 300 MS')
    expect(headline('FOR', { k_start: 0, k_end: 3, k_step: 1 })).toBe('For 0 to 3 step 1')
    expect(headline('LATCH', { mode: 'SET_DOMINANT' })).toBe('Latch Set dominant')
    expect(headline('EDGE', { edge_type: 'RISING', change_by: 1 })).toBe('RISING edge, change 1')
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
    expect(blockHeadline(catalog.block('FOR'), { id: 'b', type: 'FOR', settings: { k_start: 0, k_end: 3, k_step: 1 }, inputs: [null, { root: 'n' }] })).toBe('For 0 to n step 1')
  })

  it('has none for a block without settings worth showing', () => {
    expect(headline('IF', {})).toBeUndefined()
  })
})
