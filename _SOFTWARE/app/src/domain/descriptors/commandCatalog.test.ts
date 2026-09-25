import { describe, expect, it } from 'vitest'
import { decodeResponseData, packCommand, runitCommandCatalog, runitErrorCatalog, runitInterfaceProtocol, runitStreamCatalog } from '.'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitCommandCatalog()
const command = (id: string) => {
  const found = catalog.get(id)
  if (!found) throw new Error(`${id} is not in the catalog`)
  return found
}

describe('runIT command catalog', () => {
  it('loads contracts and settings with their class bytes', () => {
    expect(catalog.groups.map((group) => [group.id, group.classHeader])).toEqual(expect.arrayContaining([['system', 0x01], ['events', 0x09], ['ble', 0x02], ['power', 0x08]]))
    expect(command('packet_sys_io_toggle_t')).toMatchObject({ name: 'sys_io_toggle', classHeader: 0x01, packetHeader: 0x24 })
  })

  // Example frames from components/system/sys_interface/SYS_INTERFACE.MD, without the seq byte.
  it('packs the documented example commands', () => {
    expect(hex(packCommand(command('packet_sys_io_toggle_t'), { device_id: 3, pin: 4 }))).toBe('01 24 03 04')
    expect(hex(packCommand(command('packet_sys_io_set_level_t'), { device_id: 0, pin: 5, level: 1 }))).toBe('01 22 00 05 01')
    expect(hex(packCommand(command('packet_sys_io_set_pwm_duty_t'), { device_id: 3, pin: 4, duty: 1000 }))).toBe('01 28 03 04 e8 03 00 00')
  })

  it('sends every field: optional ones as their sentinel or 0, missing required ones fail', () => {
    const subscribe = command('packet_sys_event_subscribe_t')
    expect(hex(packCommand(subscribe, { domain: 0, device_id: 255, channel: 255, event: 255 }))).toBe('09 01 00 ff ff ff 00 00 00')
    expect(() => packCommand(subscribe, { domain: 0 })).toThrow(/required field 'device_id'/)
    expect(() => packCommand(subscribe, { domain: 0, device_id: 1, channel: 1, event: 1, bogus: 1 })).toThrow(/unknown fields bogus/)
  })

  it('packs a trailing text field NUL-terminated', () => {
    const create = command('packet_settings_data_connector_create_t')
    expect(hex(packCommand(create, { id: 7, header: 0x20, max_packet_len: 64, name: 'ab' }))).toBe('06 01 07 20 40 00 61 62 00')
  })

  it('decodes a response layout and keeps unknown trailing bytes', () => {
    const decoded = decodeResponseData(command('packet_sys_io_get_voltage_t'), Uint8Array.from([2, 5, 0xe8, 0x03, 0, 0, 0xaa]))
    expect(decoded?.values).toEqual({ device_id: 2, pin: 5, voltage_mV: 1000 })
    expect(hex(decoded!.extra)).toBe('aa')
    expect(decodeResponseData(command('packet_sys_io_toggle_t'), new Uint8Array())).toBeUndefined()
    expect(() => decodeResponseData(command('packet_sys_io_get_voltage_t'), Uint8Array.from([2, 5, 0xe8]))).toThrow(/voltage_mV/)
  })
})

describe('stream, error and protocol catalogs', () => {
  it('reads the command envelope from contracts, streams and enums', () => {
    const protocol = runitInterfaceProtocol()
    expect(protocol.responseStream).toBe(runitStreamCatalog().require('interface').header)
    expect(Number.isInteger(protocol.statusOk)).toBe(true)
  })

  it('builds the BLE layout from the board bindings', () => {
    const { ble } = runitStreamCatalog()
    expect(ble.notify).toContain(runitStreamCatalog().require('logs').ble?.notify)
    expect(ble.notify).not.toContain(ble.commandWrite)
  })

  it('checks the error stream against the errors connector', () => {
    expect(runitErrorCatalog().stream).toBe(runitStreamCatalog().require('errors').header)
    expect(runitErrorCatalog().tagByName('ERR_DEV_NOT_FOUND')?.name).toBe('ERR_DEV_NOT_FOUND')
  })
})
