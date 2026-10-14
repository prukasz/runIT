import { describe, expect, it } from 'vitest'
import { bestMatch, matchesSearch, searchScore } from '../../src/components/PaletteSearch/search'

describe('palette search', () => {
  it('matches names only, ignoring case, spaces and underscores', () => {
    expect(matchesSearch('Bit Expression', 'for')).toBe(false)
    expect(matchesSearch('motor_speed', 'motor speed')).toBe(true)
    expect(matchesSearch('For', ' fo ')).toBe(true)
  })

  it('ranks the whole name over its start, a word start and the inside', () => {
    expect([searchScore('For', 'for'), searchScore('Format', 'for'), searchScore('Set format', 'for'), searchScore('Platform', 'for')]).toEqual([4, 3, 2, 1])
  })

  it('the best match is the highest ranked, the first of equals', () => {
    expect(bestMatch(['Platform', 'Format', 'For', 'Forward'], (name) => name, 'for')).toBe('For')
    expect(bestMatch(['Every', 'Edge'], (name) => name, 'e')).toBe('Every')
    expect(bestMatch(['Every'], (name) => name, 'xyz')).toBeUndefined()
  })
})
