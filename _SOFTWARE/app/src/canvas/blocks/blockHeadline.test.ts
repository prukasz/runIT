import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../domain/descriptors'
import { blockHeadline } from './blockView'

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

  it('has none for a block without settings worth showing', () => {
    expect(headline('IF', {})).toBeUndefined()
  })
})
