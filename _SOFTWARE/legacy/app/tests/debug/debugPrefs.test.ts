import { describe, expect, it } from 'vitest'
import { DEFAULT_HOLD_MS, getDebugHold, MAX_HOLD_MS, setDebugHold } from '../../src/debug/debugPrefs'

describe('debug hold setting', () => {
  it('starts at 200 ms', () => {
    expect(DEFAULT_HOLD_MS).toBe(200)
    expect(getDebugHold()).toBe(200)
  })

  it('keeps what is set within 0 .. 5000, whole milliseconds', () => {
    for (const [set, expected] of [[350, 350], [-20, 0], [99999, MAX_HOLD_MS], [12.6, 13], [Number.NaN, DEFAULT_HOLD_MS]] as const) {
      setDebugHold(set)
      expect(getDebugHold()).toBe(expected)
    }
  })
})
