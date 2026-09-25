import { describe, expect, it } from 'vitest'
import board from '@data-structures/board/board.generated.json'
import enums from '@data-structures/enums.json'
import { runitCommandCatalog, runitErrorCatalog, runitStreamCatalog, runitValueNames } from '../descriptors'
import type { ErrorTagInfo } from '../descriptors'
import { decodeBoardFrame, decodeErrorPacket, decodeLogFrame, formatPrintf, parseLogLine } from '.'

const errors = runitErrorCatalog()
const streams = runitStreamCatalog()
const names = runitValueNames()

const tag = (name: string): ErrorTagInfo => {
  const found = errors.tagByName(name)
  if (!found) throw new Error(`${name} is not in the error catalog`)
  return found
}

/** A node's payload with the catalog's native layout. */
const payload = (info: ErrorTagInfo, values: Readonly<Record<string, number>>): number[] => {
  const bytes = new Uint8Array(info.payloadSize)
  const view = new DataView(bytes.buffer)
  for (const field of info.fields) {
    const value = values[field.name] ?? 0
    switch (field.type) {
      case 'uint8_t': view.setUint8(field.offset, value); break
      case 'int8_t': view.setInt8(field.offset, value); break
      case 'uint16_t': view.setUint16(field.offset, value, true); break
      case 'int16_t': view.setInt16(field.offset, value, true); break
      case 'uint32_t': view.setUint32(field.offset, value, true); break
      case 'int32_t': view.setInt32(field.offset, value, true); break
      case 'float': view.setFloat32(field.offset, value, true); break
      default: throw new Error(`test helper lacks ${field.type}`)
    }
  }
  return [...bytes]
}

/** [stream][node_count][depth][u32 schema][nodes: len, u16 tag, u16 owner, payload]. */
const errorFrame = (nodes: readonly { tag: ErrorTagInfo; owner: number; payload: number[] }[], options: { depth?: number; schemaId?: number } = {}): Uint8Array => {
  const schema = options.schemaId ?? errors.schemaId
  const bytes = [errors.stream, nodes.length, options.depth ?? nodes.length, schema & 0xff, (schema >>> 8) & 0xff, (schema >>> 16) & 0xff, (schema >>> 24) & 0xff]
  for (const node of nodes) bytes.push(node.payload.length, node.tag.id & 0xff, node.tag.id >> 8, node.owner & 0xff, node.owner >> 8, ...node.payload)
  return Uint8Array.from(bytes)
}

const anyOwner = (): number => {
  const owner = [0xa600, 0xa601, 0xa100].find((id) => errors.owner(id))
  if (owner === undefined) throw new Error('no known owner for the test')
  return owner
}

describe('formatPrintf', () => {
  it('formats like C snprintf', () => {
    expect(formatPrintf('value %lu out of range [%lu, %lu]', [7, 0, 5])).toBe('value 7 out of range [0, 5]')
    expect(formatPrintf('%ld|%d|%+d|% d', [-12, 3, 3, 3])).toBe('-12|3|+3| 3')
    expect(formatPrintf('0x%02X 0x%04x %#x %o', [10, 0xab, 255, 8])).toBe('0x0A 0x00ab 0xff 10')
    expect(formatPrintf('%u', [-1])).toBe('4294967295')
    expect(formatPrintf('%g %g %g %g', [0.5, 1e-5, 123456789, 100])).toBe('0.5 1e-05 1.23457e+08 100')
    expect(formatPrintf('%.2f %5s|%-5s|%%', [3.14159, 'ab', 'cd'])).toBe('3.14    ab|cd   |%')
    expect(formatPrintf('%s and %u', ['one'])).toBe('one and ?')
  })
})

describe('decodeErrorPacket', () => {
  it('names tags and owners and renders the firmware message', () => {
    const tooLong = tag('ERR_INTERFACE_RESPONSE_TOO_LONG')
    const owner = anyOwner()
    const report = decodeErrorPacket(errorFrame([{ tag: tooLong, owner, payload: payload(tooLong, { got: 130, max: 128 }) }]), errors)
    expect(report).toMatchObject({ schemaMatches: true, truncated: false, corrupt: false, malformed: undefined })
    expect(report.nodes[0]).toMatchObject({ tagId: tooLong.id, ownerId: owner, fields: { got: 130, max: 128 }, payloadMismatch: false })
    expect(report.nodes[0].tag?.name).toBe('ERR_INTERFACE_RESPONSE_TOO_LONG')
    expect(report.nodes[0].owner?.name).toMatch(/^OWNER_/)
    expect(report.nodes[0].level?.alias).toBeTruthy()
    expect(report.nodes[0].message).toBe('response data too long: 130 bytes, max 128')
  })

  it('prints names from the published value tables', () => {
    const control = tag('ERR_VM_EXEC_CONTROL')
    const report = decodeErrorPacket(errorFrame([{ tag: control, owner: anyOwner(), payload: payload(control, { command: 6, mode: 2 }) }]), errors)
    expect(report.nodes[0].message).toContain('(PAUSE)')
    expect(report.nodes[0].message).toContain('(FROZEN)')
  })

  it('keeps a chain in order and flags truncation, schema and payload mismatches', () => {
    const outer = tag('ERR_DEP_FAILED')
    const root = tag('ERR_INVALID_VAL_UI32')
    const frame = errorFrame([
      { tag: outer, owner: anyOwner(), payload: payload(outer, {}) },
      { tag: root, owner: anyOwner(), payload: payload(root, { val: 9, min: 1, max: 5 }).slice(0, 8) },
    ], { depth: 5, schemaId: errors.schemaId ^ 1 })
    const report = decodeErrorPacket(frame, errors)
    expect(report.nodes.map((node) => node.tag?.name)).toEqual(['ERR_DEP_FAILED', 'ERR_INVALID_VAL_UI32'])
    expect(report).toMatchObject({ truncated: true, schemaMatches: false })
    expect(report.nodes[1].payloadMismatch).toBe(true)
    expect(report.nodes[1].fields).toEqual({ val: 9, min: 1 })
    expect(report.nodes[1].message).toBe('val=9, min=1')
  })

  it('survives unknown tags and a cut frame', () => {
    const frame = Uint8Array.from([errors.stream, 2, 2, 0, 0, 0, 0, 2, 0xee, 0xee, 0x01, 0x00, 0xaa, 0xbb, 3, 1])
    const report = decodeErrorPacket(frame, errors)
    expect(report.nodes).toHaveLength(1)
    expect(report.nodes[0]).toMatchObject({ tagId: 0xeeee, tag: undefined, message: 'payload aa bb' })
    expect(report.malformed).toMatch(/node 1/)
    expect(decodeErrorPacket(Uint8Array.from([errors.stream, 0, errors.depthCorrupt, 0, 0, 0, 0]), errors).corrupt).toBe(true)
  })
})

describe('named payload fields', () => {
  const decodeOne = (name: string, values: Readonly<Record<string, number>>) => {
    const info = tag(name)
    return decodeErrorPacket(errorFrame([{ tag: info, owner: anyOwner(), payload: payload(info, values) }]), errors, names).nodes[0]
  }

  it('names board devices and enum members in the message and the labels', () => {
    const device = board.devices.at(-1)!
    expect(decodeOne('ERR_DEV_NOT_FOUND', { dev_id: device.id }).message).toBe(`device ${device.id} (${device.name}) is not registered`)
    const mode = enums.enums.sys_io_mode_e.members[1]
    const node = decodeOne('ERR_IO_PIN_MODE_UNSUPPORTED', { dev_id: device.id, pin_id: 4, mode: mode.value })
    expect(node.labels).toEqual({ dev_id: device.name, mode: mode.alias ?? mode.name })
    expect(node.message).toContain(`mode ${mode.value} (${mode.alias ?? mode.name})`)
  })

  it('renders a u32 after padding (PWM timer pool refusal)', () => {
    const device = board.devices[0]
    const node = decodeOne('ERR_IO_PWM_TIMERS_EXHAUSTED', { dev_id: device.id, pin_num: 5, timers: 4, frequency_Hz: 25000 })
    expect(node.message).toBe(`no PWM timer for 25000 Hz on pin 5, device ${device.id} (${device.name}): all 4 timers run other frequencies`)
  })

  it('names a contract feature from its sibling contract type', () => {
    const device = board.devices.at(-1)!
    const io = enums.enums.sys_device_contract_type_e.members[0]
    const node = decodeOne('ERR_DEV_FEATURE_UNAVAILABLE', { dev_id: device.id, contract_id: io.value, feature_id: 3 })
    expect(node.labels).toEqual({ dev_id: device.name, contract_id: io.alias, feature_id: errors.contractFeature(io.value, 3) })
    expect(node.labels.feature_id).toMatch(/^\w+$/)
    expect(node.message).toContain(`feature_id=3 (${node.labels.feature_id})`)
  })

  it('names a command packet from its sibling class byte', () => {
    const command = runitCommandCatalog().commands[0]
    const node = decodeOne('ERR_INTERFACE_UNKNOWN_PACKET', { class_header: command.classHeader, packet_header: command.packetHeader })
    expect(node.labels).toEqual({ class_header: command.group.title, packet_header: command.name })
  })

  it('names error tags, owners and esp_err_t codes', () => {
    const rootTag = tag('ERR_DEV_NOT_FOUND')
    const node = decodeOne('ERR_VM_EXEC_FAULT_LATCHED', { device_id: 250, root_tag: rootTag.id, root_owner: anyOwner() })
    expect(node.labels.root_tag).toBe('ERR_DEV_NOT_FOUND')
    expect(node.labels.root_owner).toMatch(/^OWNER_/)
    expect(node.labels.device_id).toBeUndefined() // not a board device: stays a number
    expect(decodeOne('ERR_ESP_ERR', { esp_code: 0x101 }).message).toBe('ESP-IDF error ESP_ERR_NO_MEM (0x101)')
  })

  it('leaves values unnamed without a resolver', () => {
    const info = tag('ERR_DEV_NOT_FOUND')
    const node = decodeErrorPacket(errorFrame([{ tag: info, owner: anyOwner(), payload: payload(info, { dev_id: board.devices[0].id }) }]), errors).nodes[0]
    expect(node.labels).toEqual({})
    expect(node.message).toBe(`device ${board.devices[0].id} is not registered`)
  })
})

describe('logs', () => {
  it('parses ESP-IDF lines, error-chain lines and plain text', () => {
    expect(parseLogLine('W (1234) runit_board: rail A low')).toEqual({ kind: 'esp', level: 'warn', timestampMs: 1234, tag: 'runit_board', text: 'rail A low' })
    expect(parseLogLine('\u001b[0;31mE (5) sys: bad\u001b[0m')).toMatchObject({ kind: 'esp', level: 'error', text: 'bad' })
    expect(parseLogLine('[1] owner=OWNER_SYS_IO_GET (0xA302) tag=ERR_DEV_NOT_FOUND (41217): device 77 not found')).toEqual({
      kind: 'error-chain', depth: 1, owner: 'OWNER_SYS_IO_GET', ownerId: 0xa302, tag: 'ERR_DEV_NOT_FOUND', tagId: 41217, text: 'device 77 not found',
    })
    expect(parseLogLine('<error chain truncated or corrupt>')).toEqual({ kind: 'text', text: '<error chain truncated or corrupt>' })
    expect(decodeLogFrame(new TextEncoder().encode('I (1) a: x\r\n\nI (2) b: y\n'))).toHaveLength(2)
  })
})

describe('decodeBoardFrame', () => {
  it('routes frames by the published stream bytes', () => {
    const catalogs = { streams, errors }
    const logs = streams.require('logs')
    expect(decodeBoardFrame(Uint8Array.from([logs.header, ...new TextEncoder().encode('I (9) t: hi\n')]), catalogs)).toMatchObject({ kind: 'logs', entries: [{ kind: 'esp', text: 'hi' }] })
    expect(decodeBoardFrame(errorFrame([]), catalogs)).toMatchObject({ kind: 'errors', report: { nodes: [] } })
    expect(decodeBoardFrame(Uint8Array.from([streams.require('telemetry').header, 1]), catalogs)).toMatchObject({ kind: 'other' })
    const unused = [...Array(256).keys()].find((byte) => !streams.byHeader(byte))!
    expect(decodeBoardFrame(Uint8Array.from([unused, 1]), catalogs)).toMatchObject({ kind: 'unknown', header: unused })
  })
})
