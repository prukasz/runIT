import { describe, expect, it } from 'vitest'
import capture from './fixtures/ble-errors.capture.json'
import { runitErrorCatalog, runitStreamCatalog, runitValueNames } from '../descriptors'
import { decodeBoardFrame } from '.'
import type { ErrorReport } from './errorPacket'
import type { LogEntry } from './logLines'

/**
 * Frames captured from the PCB over BLE (.claude/skills/runit-esp/scripts/error_capture.py):
 * commands the board refuses, each raising an error whose payload carries named IDs.
 * The capture keeps the schema ID of the firmware it came from (it matched the
 * generator's on 2026-09-25); tag and owner IDs are stable, so it must keep
 * decoding completely after the maps grow.
 */
const CAPTURE_SCHEMA_ID = 0x5ef7b35c

const catalogs = { streams: runitStreamCatalog(), errors: runitErrorCatalog() }
const named = { ...catalogs, names: runitValueNames() }

const bytes = (hex: string): Uint8Array => Uint8Array.from(hex.match(/../g) ?? [], (byte) => parseInt(byte, 16))

type ChainLine = Extract<LogEntry, { kind: 'error-chain' }>

/** Each error packet with the board's own error-chain log lines sent just before it. */
const pairs = (): { report: ErrorReport; named: ErrorReport; lines: ChainLine[] }[] => {
  const out: { report: ErrorReport; named: ErrorReport; lines: ChainLine[] }[] = []
  let lines: ChainLine[] = []
  for (const frame of capture.frames) {
    const decoded = decodeBoardFrame(bytes(frame.hex), catalogs)
    if (decoded.kind === 'logs') lines.push(...decoded.entries.filter((entry): entry is ChainLine => entry.kind === 'error-chain'))
    if (decoded.kind === 'errors') {
      const withNames = decodeBoardFrame(bytes(frame.hex), named)
      if (withNames.kind !== 'errors') throw new Error('frame decoded differently with names')
      out.push({ report: decoded.report, named: withNames.report, lines })
      lines = []
    }
  }
  return out
}

describe('live board capture', () => {
  const errors = pairs()

  it('has error packets from every case', () => {
    expect(errors.length).toBe(7)
  })

  it('decodes every node of the captured schema', () => {
    for (const { report } of errors) {
      expect(report.schemaId).toBe(CAPTURE_SCHEMA_ID)
      expect(report.truncated || report.corrupt || report.malformed !== undefined).toBe(false)
      for (const node of report.nodes) {
        expect(node.tag, `tag 0x${node.tagId.toString(16)}`).toBeDefined()
        expect(node.owner, `owner 0x${node.ownerId.toString(16)}`).toBeDefined()
        expect(node.level).toBeDefined()
        expect(node.payloadMismatch).toBe(false)
      }
    }
  })

  it('renders the same text as the firmware for every tag with a message template', () => {
    let compared = 0
    for (const { named: report, lines } of errors) {
      expect(lines.map((line) => line.tag)).toEqual(report.nodes.map((node) => node.tag?.name))
      report.nodes.forEach((node, depth) => {
        if (!node.tag?.message) return
        // The app adds ` (name)` after an ID it names; the firmware prints the bare ID.
        const text = Object.values(node.labels).reduce((message, label) => message.replaceAll(` (${label})`, ''), node.message)
        expect(text).toBe(lines[depth].text)
        compared++
      })
    }
    expect(compared).toBeGreaterThanOrEqual(10)
  })

  it('names the IDs in the payloads', () => {
    const labels = errors.flatMap(({ named: report }) => report.nodes.map((node) => [node.tag?.name, node.labels] as const))
    const of = (tag: string) => labels.filter(([name]) => name === tag).map(([, found]) => found)
    expect(of('ERR_DEV_FEATURE_UNAVAILABLE')).toEqual([expect.objectContaining({ dev_id: 'INA3221', feature_id: 'set_level' })])
    expect(of('ERR_ESP_ERR')).toEqual([{ esp_code: 'ESP_ERR_INVALID_ARG' }])
    expect(of('ERR_INTERFACE_UNKNOWN_PACKET')).toEqual([{ class_header: 'System contracts' }])
    // IDs the board doesn't have stay unnamed.
    expect(of('ERR_DEV_NOT_FOUND')).toEqual([{}])
    expect(of('ERR_INTERFACE_UNKNOWN_CLASS')).toEqual([{}])
  })
})
