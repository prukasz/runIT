import { describe, expect, it } from 'vitest'
import { bundledLinkDefinitions, bundledServiceCatalog, deviceCall, requestFrame } from '../../src/backend'
import { encodeInput, parseHex, showBytes } from '../../src/app/console/serial'

describe('the serial terminal', () => {
  it('turns what is typed into bytes: a text line with its ending, hex as it is', () => {
    expect([...encodeInput('AT', 'text', 'crlf')]).toEqual([0x41, 0x54, 0x0d, 0x0a])
    expect([...encodeInput('AT', 'text', 'none')]).toEqual([0x41, 0x54])
    expect([...encodeInput('b5 62, 0x06', 'hex', 'crlf')]).toEqual([0xb5, 0x62, 0x06])
    expect(() => parseHex('b5 6')).toThrow(/two digits/)
    expect(() => parseHex('zz')).toThrow(/digits/)
  })

  it('shows what comes back as text when it can, else as hex', () => {
    expect(showBytes(new TextEncoder().encode('OK\r\n'))).toBe('OK')
    expect(showBytes(Uint8Array.of(0xb5, 0x62, 0x00))).toBe('B5 62 00')
  })

  it('builds the commands of the UART contract as the descriptor lays them out: TRANSACT, SEND, RECEIVE of a device', () => {
    const catalog = bundledServiceCatalog()
    const defs = bundledLinkDefinitions()
    const transact = deviceCall(defs, catalog, 7, 'UART', 'TRANSACT', { wait_ms: 300, data: Uint8Array.of(0x41) }, 'write')
    // [key][write][device u8][op u16: contract 19, TRANSACT 6]: then wait_ms u16, the bytes
    expect(Array.from(requestFrame(transact.request).slice(2))).toEqual([7, 0x06, 0x13, 0x2c, 0x01, 0x41])
    const send = deviceCall(defs, catalog, 7, 'UART', 'SEND', { wait_ms: 0, flags: 0, data: Uint8Array.of(0x41) }, 'write')
    expect(Array.from(requestFrame(send.request).slice(2))).toEqual([7, 0x02, 0x13, 0x00, 0x00, 0x00, 0x41])
    const receive = deviceCall(defs, catalog, 7, 'UART', 'RECEIVE', { wait_ms: 300 }, 'read')
    expect(Array.from(requestFrame(receive.request).slice(2))).toEqual([7, 0x03, 0x13, 0x2c, 0x01])
  })
})
