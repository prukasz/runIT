import { describe, expect, it } from 'vitest'
import { bundledLinkDefinitions, bundledServiceCatalog, readDeviceFrame } from '../../src/backend'
import { emptyProject, parseProject, serializeProject } from '../../src/app/project/projectFile'
import type { ProjectFile } from '../../src/app/project/projectFile'
import { boardType, deviceList, deviceRecordOf, deviceSections, hydrateRemoteDevices, SINGLE_TYPES } from '../../src/app/devices/devices'
import { createEngine } from '../../src/engine'
import { blockPinChoices, deviceChoicesFor } from '../../src/app/code/useBlocks'
import { buildProviderRegistry } from '../../src/app/routing/providerRegistry'
import { FIRST_LINK_PROVIDER, LAST_LINK_PROVIDER, addressBytes, boardProblems, boardSections, formatAddress, rememberWiredBoard } from '../../src/app/boards/boards'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'

const defs = bundledLinkDefinitions()
const catalog = bundledServiceCatalog()
const board = bundledBoard()
const gpio = board.devices.find((d) => SINGLE_TYPES.has(boardType(catalog, d.descriptor)?.id ?? ''))!
const gpioType = boardType(catalog, gpio.descriptor)!.id

/** A master and one slave: the slave's GPIO as device 20 here, its pin 5 an output at start; a servo on the slave on pin 6. */
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

  it('the register gives a new link the lowest provider id nothing holds', () => {
    const registry = buildProviderRegistry({ live: [], project, devices: [] })
    expect(registry.freeFrom(FIRST_LINK_PROVIDER, LAST_LINK_PROVIDER)).toBe(0x31)
  })

  it('a device on another board is only its alias here: CREATE_REMOTE of its type, board and id there', () => {
    const sections = deviceSections(defs, catalog, project, board)
    const alias = sections.find((s) => s.id === deviceRecordOf(20))!
    expect(alias.frames).toHaveLength(1)
    const read = readDeviceFrame(catalog, alias.frames[0]!)!
    expect(read).toMatchObject({ device: 20, side: 'write', command: { id: 'CREATE_REMOTE' } })
    expect(read.args).toEqual({ type: catalog.device(gpioType).typeId, board: 2, remote_id: gpio.id, flags: catalog.remoteFlags.wireless })
  })

  it('on the other board its own GPIO is only set up, and the servo is created on a pin of that GPIO', () => {
    const sections = boardSections(defs, catalog, project, board, 2)
    expect(sections.map((s) => s.id)).toEqual([deviceRecordOf(gpio.id), deviceRecordOf(21)])
    // The MODE of the GPIO names the device by its id there.
    const mode = sections[0]!.frames
    expect(mode).toHaveLength(1)
    expect(readDeviceFrame(catalog, mode[0]!)?.device).toBe(gpio.id)
    // CREATE of the servo under 21 (no remoteId: the same id there), its pwm_pin on device gpio.id, not 20.
    const create = readDeviceFrame(catalog, sections[1]!.frames[0]!)!
    expect(create.device).toBe(21)
    expect(create.create?.config?.pwm_pin).toEqual({ device: gpio.id, pin: 6 })
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

  it('remembers a detected wired board, round-trips it without a BLE address, and creates WIRED aliases for its devices', () => {
    const base = emptyProject('Wired')
    const first = rememberWiredBoard(base, {
      id: 1,
      name: 'runIT devkit',
      profile: 'runIT devkit, 3 devices',
      knownDevices: [{ id: gpio.id, type: gpioType, name: 'ESP32-S3 GPIO' }],
    })!
    expect(first.boards).toEqual([
      { id: 1, name: 'runIT devkit', link: 'wired', profile: 'runIT devkit, 3 devices', knownDevices: [{ id: gpio.id, type: gpioType, name: 'ESP32-S3 GPIO' }] },
    ])
    // Same detection again changes nothing.
    expect(rememberWiredBoard(first, {
      id: 1,
      name: 'runIT devkit',
      profile: 'runIT devkit, 3 devices',
      knownDevices: [{ id: gpio.id, type: gpioType, name: 'ESP32-S3 GPIO' }],
    })).toBeUndefined()
    // Preserves a custom name and moves devices if the detected board id changed.
    const renamed: ProjectFile = {
      ...first,
      boards: first.boards!.map((b) => ({ ...b, name: 'Slave A' })),
      devices: [{ id: 20, type: gpioType, name: 'Slave A GPIO', board: 1, remoteId: gpio.id }],
    }
    const moved = rememberWiredBoard(renamed, { id: 3, name: 'runIT devkit', profile: 'runIT devkit, 3 devices' })!
    expect(moved.boards![0]).toMatchObject({ id: 3, name: 'Slave A', link: 'wired' })
    expect(moved.devices[0]!.board).toBe(3)
    expect(boardProblems(moved, 2, catalog)).toEqual([])
    const parsed = parseProject(serializeProject(moved))
    expect(parsed.ok && parsed.project.boards).toEqual(moved.boards)
    // CREATE_REMOTE on a wired board carries the WIRED flag.
    const sections = deviceSections(defs, catalog, moved, board)
    const alias = sections.find((s) => s.id === deviceRecordOf(20))!
    expect(readDeviceFrame(catalog, alias.frames[0]!)!.args).toEqual({
      type: catalog.device(gpioType).typeId,
      board: 3,
      remote_id: gpio.id,
      flags: catalog.remoteFlags.wired,
    })
    // Hydrated with the remote board's actual pin modes (pin 4 input, pin 5 output), Code blocks filter to only those pins.
    const remoteBoard = bundledBoard('devkit')
    const remoteProject: ProjectFile = {
      ...emptyProject('Devkit'),
      boardDefaults: [{
        device: gpio.id,
        steps: [
          { kind: 'device', device: gpio.id, contract: 'IO', command: 'MODE', args: { pin: 4, mode: 0 } }, // INPUT
          { kind: 'device', device: gpio.id, contract: 'IO', command: 'MODE', args: { pin: 5, mode: 3 } }, // OUTPUT
        ],
      }],
    }
    const remoteCtx = { board: remoteBoard, devices: deviceList(remoteProject, catalog, remoteBoard) }
    const engine = createEngine()
    const ioType = engine.catalog.require('io')
    const hydrated = hydrateRemoteDevices(deviceList(moved, catalog, board), moved, remoteCtx)
    const state = { catalog, board, devices: hydrated }
    const remoteGpio = hydrated.find((d) => d.id === 20)!
    expect(deviceChoicesFor(state, ioType, 'get_level').some((d) => d.id === 20)).toBe(true)
    expect(blockPinChoices(state, ioType, 'get_level', 'pin', remoteGpio)).toEqual([4, 5])
    expect(blockPinChoices(state, ioType, 'set_level', 'pin', remoteGpio)).toEqual([5])
    expect(deviceChoicesFor(state, ioType, 'get_voltage').some((d) => d.id === 20)).toBe(false)
    // Upgrading an existing wireless board with the same id turns it into a wired board while keeping its name.
    const upgraded = rememberWiredBoard(project, { id: 2, name: 'runIT devkit', profile: 'runIT devkit, 3 devices' })!
    expect(upgraded.boards).toEqual([{ id: 2, name: 'Arm', link: 'wired', profile: 'runIT devkit, 3 devices' }])
  })
})
