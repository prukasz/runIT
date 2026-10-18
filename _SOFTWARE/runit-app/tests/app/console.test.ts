import { describe, expect, it } from 'vitest'
import { parseLogLine } from '../../src/app/console/useConsole'

describe('the console', () => {
  it('cuts an ESP_LOG line into its level, its uptime, its tag and its text', () => {
    expect(parseLogLine('I (854) sys_project.c: autostart: running the stored project\n')).toEqual({ level: 'I', uptimeMs: 854, source: 'sys_project.c', text: 'autostart: running the stored project' })
    expect(parseLogLine('E (863) err_log.c: cmd internal seq 0')).toMatchObject({ level: 'E', source: 'err_log.c' })
  })

  it('keeps a line that is not ESP_LOG\'s as it is', () => {
    expect(parseLogLine('rst:0x1 (POWERON)')).toEqual({ level: 'I', text: 'rst:0x1 (POWERON)' })
  })
})
