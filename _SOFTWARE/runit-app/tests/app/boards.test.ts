import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledLinkDefinitions, bundledServiceCatalog, requestFrame, serviceCall } from '../../src/backend'
import { emptyProject, parseProject, serializeProject } from '../../src/app/project/projectFile'
import type { ProjectFile } from '../../src/app/project/projectFile'
import { boardType, deviceRecordOf, deviceSections, SINGLE_TYPES } from '../../src/app/devices/devices'
import { addressBytes, boardProblems, boardSections, decodeFound, formatAddress, LINKS_RECORD, linksSection, nextLinkProvider } from '../../src/app/boards/boards'

const defs = bundledLinkDefinitions()
const catalog = bundledServiceCatalog()
const board = bundledBoard()
const gpio = board.devices.find((d) => SINGLE_TYPES.has(boardType(catalog, d.descriptor)?.id ?? ''))!
const gpioType = boardType(catalog, gpio.descriptor)!.id

/** A master and one slave over BLE: the slave's GPIO as device 20 here, its pin 5 an output at start; a servo on the slave on pin 6. */
const project: ProjectFile = {
  ...emptyProject('Two boards'),
  boards: [{ id: 2, name: 'Arm', address: 'F4:12:FA:01:02:03', addressType: 0, provider: 0x30 }],
  devices: [
    { id: 20, type: gpioType, name: 'Arm GPIO', board: 2, remoteId: gpio.id, defaults: [{ kind: 'device', device: 20, contract: 'IO', command: 'MODE', args: { pin: 5, mode: 3 } }] },
    { id: 21, type: 'servo', name: 'Arm servo', board: 2, config: { pwm_pin: { device: 20, pin: 6 }, servo_type: 0, byte_mode: 0, invert: 0, angle_min_deg: 0, angle_max_deg: 180, default_pos: 90, min_us: 500, max_us: 2400, deadband_us: 0 } },
  ],
}

const hex = (bytes: Uint8Array) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(' ')

describe('the other boards of a project', () => {
  it('shows an address most significant byte first and sends it least significant first', () => {
    expect(formatAddress([3, 2, 1, 0xfa, 0x12, 0xf4])).toBe('F4:12:FA:01:02:03')
    expect(addressBytes('f4:12:fa:01:02:03')).toEqual([3, 2, 1, 0xfa, 0x12, 0xf4])
    expect(addressBytes('F4:12:FA')).toBeUndefined()
  })

  it('reads FOUND: the scan still running and each board (sys_ble_found_info_t, 24 B)', () => {
    const entry = [1, 3, 2, 1, 0xfa, 0x12, 0xf4, 0xc4, ...new TextEncoder().encode('runit'), ...new Array(11).fill(0)]
    const found = decodeFound(Uint8Array.from([1, 1, ...entry]))
    expect(found.scanning).toBe(true)
    expect(found.boards).toEqual([{ addressType: 1, address: 'F4:12:FA:01:02:03', rssi: -60, name: 'runit' }])
    expect(decodeFound(Uint8Array.of(0, 0))).toEqual({ scanning: false, boards: [] })
  })

  it('links each board at start: BLE CONNECT of its provider and address, then SESSION ATTACH as SLAVE', () => {
    const section = linksSection(defs, catalog, project)!
    expect(section.id).toBe(LINKS_RECORD)
    expect(section.frames.map(hex)).toEqual([
      hex(requestFrame(serviceCall(catalog, 'BLE', 'CONNECT', { provider: 0x30, addr_type: 0, addr_lo: 0xfa010203, addr_hi: 0xf412 }, 'write').request)),
      hex(requestFrame(serviceCall(catalog, 'SESSION', 'ATTACH', { provider: 0x30, peer: defs.session.peer.slave }, 'write').request)),
    ])
    // [BLE key][write][CONNECT][provider][type][address, least significant first]
    expect(hex(section.frames[0]!)).toBe(hex(Uint8Array.of(defs.keys.ble!, defs.dir.write, 0x0a, 0x30, 0, 3, 2, 1, 0xfa, 0x12, 0xf4)))
    expect(linksSection(defs, catalog, emptyProject())).toBeUndefined()
    expect(nextLinkProvider(project)).toBe(0x31)
  })

  it('a device on another board is only its alias here: CREATE_REMOTE of its type, board and id there', () => {
    const sections = deviceSections(defs, catalog, project, board)
    const alias = sections.find((s) => s.id === deviceRecordOf(20))!
    expect(alias.frames).toHaveLength(1)
    const frame = alias.frames[0]!
    expect([frame[0], frame[1], frame[2]! | (frame[3]! << 8)]).toEqual([defs.keys.device!, defs.dir.write, 20])
    // After [key][dir][id u16][op u16]: type u16, board u8, remote id u16, flags u8 (contract_device.json CREATE_REMOTE).
    const typeId = catalog.device(gpioType).typeId!
    expect(Array.from(frame.subarray(6))).toEqual([typeId & 0xff, typeId >> 8, 2, gpio.id & 0xff, gpio.id >> 8, 0])
  })

  it('on the other board its own GPIO is only set up, and the servo is created on a pin of that GPIO', () => {
    const sections = boardSections(defs, catalog, project, board, 2)
    expect(sections.map((s) => s.id)).toEqual([deviceRecordOf(gpio.id), deviceRecordOf(21)])
    // The MODE of the GPIO names the device by its id there.
    const mode = sections[0]!.frames
    expect(mode).toHaveLength(1)
    expect(mode[0]![2]! | (mode[0]![3]! << 8)).toBe(gpio.id)
    // CREATE of the servo under 21 (no remoteId: the same id there), its pwm_pin on device gpio.id, not 20.
    const create = sections[1]!.frames[0]!
    expect(create[2]! | (create[3]! << 8)).toBe(21)
    expect(hex(create)).toContain(hex(Uint8Array.of(gpio.id & 0xff, gpio.id >> 8, 6)))
    expect(boardSections(defs, catalog, project, board, 3)).toEqual([])
  })

  it('names what is wrong: the id of the board the app is on, a pin of another board, a board the project lacks', () => {
    expect(boardProblems(project, 1, catalog)).toEqual([])
    expect(boardProblems(project, 2, catalog)[0]).toMatch(/id of the board the app is connected to/)
    const crossing: ProjectFile = { ...project, devices: [...project.devices, { id: 22, type: 'servo', name: 'Here servo', config: { pwm_pin: { device: 20, pin: 7 } } }] }
    expect(boardProblems(crossing, 1, catalog).some((p) => p.startsWith('Here servo'))).toBe(true)
    expect(boardProblems({ ...project, boards: [] }, 1, catalog).some((p) => /does not have/.test(p))).toBe(true)
  })

  it('keeps the boards and where each device is in the project file', () => {
    const parsed = parseProject(serializeProject(project))
    expect(parsed.ok && parsed.project.boards).toEqual(project.boards)
    expect(parsed.ok && parsed.project.devices[0]).toMatchObject({ board: 2, remoteId: gpio.id })
  })
})
