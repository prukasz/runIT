import { describe, expect, it } from 'vitest'
import { matchesSearch } from '../src/ObjectTreeWorkspace'

describe('matchesSearch', () => {
  it('returns false for empty or whitespace query', () => {
    expect(matchesSearch('my_var', '')).toBe(false)
    expect(matchesSearch('my_var', '   ')).toBe(false)
  })

  it('matches exact name and case-insensitively', () => {
    expect(matchesSearch('sensor', 'sensor')).toBe(true)
    expect(matchesSearch('sensor', 'SENSOR')).toBe(true)
    expect(matchesSearch('Sensor', 'sensor')).toBe(true)
    expect(matchesSearch('Sensor', 'SeNsOr')).toBe(true)
  })

  it('matches containing word (substring match)', () => {
    expect(matchesSearch('temperature_sensor', 'temp')).toBe(true)
    expect(matchesSearch('temperature_sensor', 'sensor')).toBe(true)
    expect(matchesSearch('temperature_sensor', 'ature')).toBe(true)
    expect(matchesSearch('temperature_sensor', 'pressure')).toBe(false)
  })

  it('ignores spaces in query and in target name', () => {
    expect(matchesSearch('my variable', 'myvariable')).toBe(true)
    expect(matchesSearch('myvariable', 'my variable')).toBe(true)
    expect(matchesSearch('my   variable', 'my var')).toBe(true)
    expect(matchesSearch('my variable', '  variable  ')).toBe(true)
  })

  it('matches words separated by underscore when searching with spaces', () => {
    expect(matchesSearch('sensor_temp', 'sensor temp')).toBe(true)
    expect(matchesSearch('motor_speed_rpm', 'speed rpm')).toBe(true)
  })
})
