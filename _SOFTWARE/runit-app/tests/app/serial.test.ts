import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, serviceCall } from '../../src/backend'
import { canListen, decodeProviders, encodeInput, parseHex, providerLabel, rawProviders, showBytes } from '../../src/app/console/serial'

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

  it('reads PROVIDER LIST, and offers only the raw providers', () => {
    const framed = 1 << 5
    const list = decodeProviders(Uint8Array.of(2, 1, 1, framed, 0x10, 1, 0))
    expect(list).toEqual([{ id: 1, link: 1, state: framed }, { id: 0x10, link: 1, state: 0 }])
    expect(rawProviders(list).map((p) => p.id)).toEqual([0x10])
    expect(providerLabel(list[1]!)).toBe('Serial 0x10')
    expect(decodeProviders(Uint8Array.of(2, 1, 1))).toEqual([]) // cut short
    expect(canListen(list[1])).toBe(true)
    expect(canListen({ id: 0x11, link: 1, state: 1 << 6 })).toBe(false) // a direct port
    expect(canListen(undefined)).toBe(false)
  })

  it('builds the commands as the descriptor lays them out', () => {
    const catalog = bundledServiceCatalog()
    const raw = serviceCall(catalog, 'UART', 'RAW', { provider: 0x10, wait_ms: 300, rx_max: 256, data: Uint8Array.of(0x41) })
    expect([...raw.request.payload!]).toEqual([0x10, 0x2c, 0x01, 0x00, 0x01, 0x41])
    const receive = serviceCall(catalog, 'UART', 'RECEIVE', { provider: 0x10, wait_ms: 300, rx_max: 256 })
    expect([...receive.request.payload!]).toEqual([0x10, 0x2c, 0x01, 0x00, 0x01])
    const send = serviceCall(catalog, 'PROVIDER', 'SEND', { provider: 0x10, data: Uint8Array.of(0x41) })
    expect([...send.request.payload!]).toEqual([0x10, 0x41])
    const create = serviceCall(catalog, 'UART', 'CREATE', { provider: 0x10, port: 2, baud: 9600, mode: 2, parity: 0, stop_bits: 1, tx: { device: 0, pin: 4 }, rx: { device: 0, pin: 5 }, rts: null, cts: null }, 'write')
    expect([...create.request.payload!]).toEqual([0x10, 2, 0x80, 0x25, 0, 0, 2, 0, 1, 0, 0, 4, 0, 0, 5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff])
  })
})
