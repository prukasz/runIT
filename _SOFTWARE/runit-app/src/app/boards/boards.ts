import { bundledEnums, createDeviceCall, requestFrame, serviceCall } from '@backend'
import type { BoardDescriptor, LinkDefinitions, Section, ServiceCall, ServiceCatalog } from '@backend'
import type { ProjectBoard, ProjectDevice, ProjectFile, Step } from '../project/projectFile'
import { boardDeviceId, configPins, deviceList, deviceRecordOf, stepFrame } from '../devices/devices'

/*
 * The other boards of the project, pure (no link, no UI): boards linked to the one the app is connected to over BLE, each
 * reached through it (SYS_BLE.MD "Central", SYS_SESSION.MD "Boards, clients, the star").
 *
 *   find      BLE SCAN on this board, then FOUND: the runIT boards it sees (`decodeFound`).
 *   link      BLE CONNECT (this board's provider to that board's runIT pair) and SESSION ATTACH of that provider as SLAVE: the
 *             board's hello makes this board its master. Both are the settings record SYS_ACTIONS_ID_LINKS of the stored
 *             project (`linksSection`), so this board links again at every start.
 *   devices   a project device on another board (`ProjectDevice.board`) is created there with its config and start commands, its
 *             ids those of that board (`boardSections`), and is an alias on this board (devices.ts `remoteFrame`).
 */

/** The service commands this file sends (BLE and SESSION descriptors). */
export const BOARD_COMMANDS = {
  ble: 'BLE', scan: 'SCAN', found: 'FOUND', connect: 'CONNECT',
  session: 'SESSION', attach: 'ATTACH', detach: 'DETACH', id: 'ID',
  provider: 'PROVIDER', remove: 'REMOVE',
  system: 'SYS', restart: 'RESTART',
} as const

/** How long a scan runs (BLE SCAN takes 100 to 30000 ms). */
export const SCAN_MS = 3000

/** The first provider id the app gives a link: above the board's own (1..4) and the ports and characteristics a project makes (0x10..). */
const FIRST_LINK_PROVIDER = 0x30

/** The settings record of the links (`settings_record` in enums.json). */
export const LINKS_RECORD = (() => {
  const member = bundledEnums().settings_record?.members.find((m) => m.name === 'SYS_ACTIONS_ID_LINKS')
  if (!member) throw new Error("generated/common/enums.json: group 'settings_record' has no SYS_ACTIONS_ID_LINKS.")
  return member.value >>> 0
})()

/* ---- addresses ---- */

/** An address as the firmware sends it (least significant byte first) as it is shown: `F4:12:FA:01:02:03`. */
export const formatAddress = (bytes: ArrayLike<number>): string =>
  Array.from(bytes).reverse().map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(':')

/** The bytes of a shown address, least significant first; undefined when it is not six hex bytes. */
export const addressBytes = (address: string): number[] | undefined => {
  const parts = address.trim().split(/[:-]/)
  if (parts.length !== 6 || !parts.every((p) => /^[0-9a-f]{2}$/i.test(p))) return undefined
  return parts.map((p) => parseInt(p, 16)).reverse()
}

/* ---- the scan ---- */

/** A runIT board the scan found. */
export interface FoundBoard {
  readonly address: string
  readonly addressType: number
  /** dBm. */
  readonly rssi: number
  readonly name: string
}

/* FOUND's answer: sys_ble_found_head_t {u8 scanning, u8 count}, then count x sys_ble_found_info_t {u8 addr_type, u8 addr[6],
   i8 rssi, char name[16]} (sys_ble_router.h). */
const FOUND_HEAD = 2
const FOUND_ENTRY = 24
const FOUND_NAME = 16

/** FOUND's answer: whether the scan still runs and the boards it found. */
export const decodeFound = (data: Uint8Array): { readonly scanning: boolean; readonly boards: FoundBoard[] } => {
  if (data.length < FOUND_HEAD) return { scanning: false, boards: [] }
  const count = Math.min(data[1]!, Math.floor((data.length - FOUND_HEAD) / FOUND_ENTRY))
  const boards = Array.from({ length: count }, (_, i): FoundBoard => {
    const at = FOUND_HEAD + i * FOUND_ENTRY
    const name = data.subarray(at + 8, at + 8 + FOUND_NAME)
    const end = name.indexOf(0)
    return {
      addressType: data[at]!,
      address: formatAddress(data.subarray(at + 1, at + 7)),
      rssi: (data[at + 7]! << 24) >> 24,
      name: new TextDecoder().decode(end < 0 ? name : name.subarray(0, end)),
    }
  })
  return { scanning: data[0] === 1, boards }
}

/* ---- the links ---- */

/** The provider id a new link gets: the lowest from FIRST_LINK_PROVIDER the project's boards leave free. */
export const nextLinkProvider = (project: ProjectFile): number => {
  const taken = new Set((project.boards ?? []).map((b) => b.provider))
  let id = FIRST_LINK_PROVIDER
  while (taken.has(id)) id++
  return id
}

/** The commands that link this board to `board`: BLE CONNECT, then SESSION ATTACH as SLAVE. */
export const linkCalls = (defs: LinkDefinitions, catalog: ServiceCatalog, board: ProjectBoard): ServiceCall[] => {
  const bytes = addressBytes(board.address)
  if (!bytes) throw new Error(`${board.name}: "${board.address}" is not a BLE address (six hex bytes).`)
  const lo = (bytes[0]! | (bytes[1]! << 8) | (bytes[2]! << 16) | (bytes[3]! << 24)) >>> 0
  const hi = bytes[4]! | (bytes[5]! << 8)
  return [
    serviceCall(catalog, BOARD_COMMANDS.ble, BOARD_COMMANDS.connect, { provider: board.provider, addr_type: board.addressType, addr_lo: lo, addr_hi: hi }, 'write'),
    serviceCall(catalog, BOARD_COMMANDS.session, BOARD_COMMANDS.attach, { provider: board.provider, peer: defs.session.peer.slave }, 'write'),
  ]
}

/** The links as the settings record of the stored project; none when the project has no other board. */
export const linksSection = (defs: LinkDefinitions, catalog: ServiceCatalog, project: ProjectFile): Section | undefined => {
  const boards = project.boards ?? []
  return boards.length === 0 ? undefined : { id: LINKS_RECORD, frames: boards.flatMap((b) => linkCalls(defs, catalog, b).map((c) => requestFrame(c.request))) }
}

/**
 * What is wrong with the boards: an id twice, this board's id, a provider twice, an address that is none, a device on a board
 * the project does not have, a device using a pin of a device on another board (a pin is wired on its own board).
 */
export const boardProblems = (project: ProjectFile, ownId: number | undefined, catalog?: ServiceCatalog): string[] => {
  const boards = project.boards ?? []
  const problems: string[] = []
  boards.forEach((b, i) => {
    if (ownId !== undefined && b.id === ownId) problems.push(`${b.name} has the id of the board the app is connected to (${b.id}): give one of them another id.`)
    if (boards.findIndex((o) => o.id === b.id) !== i) problems.push(`${b.name} has the id of ${boards.find((o) => o.id === b.id)!.name} (${b.id}).`)
    if (boards.findIndex((o) => o.provider === b.provider) !== i) problems.push(`${b.name} has the link of another board (provider ${b.provider}).`)
    if (!addressBytes(b.address)) problems.push(`${b.name}: "${b.address}" is not a BLE address.`)
  })
  const boardOf = (device: number) => project.devices.find((d) => d.id === device)?.board
  for (const d of project.devices) {
    if (d.board !== undefined && !boards.some((b) => b.id === d.board)) problems.push(`${d.name} is on a board the project does not have (${d.board}).`)
    for (const use of catalog ? configPins(catalog.devices.get(d.type), d.config) : []) {
      if (boardOf(use.pin.device) !== d.board) problems.push(`${d.name}: ${use.use} is a pin of another board: a device uses the pins of its own board.`)
    }
  }
  return problems
}

/** The name of a board of the project by its id. */
export const boardNameOf = (project: ProjectFile, id: number | undefined): string | undefined =>
  id === undefined ? undefined : project.boards?.find((b) => b.id === id)?.name

/* ---- devices on another board ---- */

/** The project's devices on board `id`. */
export const devicesOn = (project: ProjectFile, id: number): ProjectDevice[] => project.devices.filter((d) => d.board === id)

/** Whether a device on another board is that board's own (board.json): it is set up there, never created. */
export const isBoardOwn = (board: BoardDescriptor, device: ProjectDevice): boolean => board.devices.some((d) => d.id === boardDeviceId(device))

/** A value with every `device` key that names a device of board `id` moved to its id there (a config's pins, a step's device). */
const onBoard = (project: ProjectFile, id: number) => {
  const ids = new Map(devicesOn(project, id).map((d) => [d.id, boardDeviceId(d)]))
  const walk = (value: unknown): unknown => {
    if (Array.isArray(value)) return value.map(walk)
    if (value === null || typeof value !== 'object') return value
    return Object.fromEntries(Object.entries(value).map(([key, v]) => [key, key === 'device' && typeof v === 'number' && ids.has(v) ? ids.get(v) : walk(v)]))
  }
  return walk
}

/** A config or a step of a device on board `id` as that board takes it: the devices it names by their ids there. */
export const onBoardValue = <T,>(project: ProjectFile, id: number, value: T): T => onBoard(project, id)(value) as T

/**
 * The device sections of board `id`'s stored project: each device of the project on it, in creation order, as that board's
 * record (`0x02000000 | its id there`): CREATE with its config (not for the board's own), then its start commands.
 */
export const boardSections = (defs: LinkDefinitions, catalog: ServiceCatalog, project: ProjectFile, board: BoardDescriptor, id: number): Section[] => {
  const there = onBoard(project, id)
  return deviceList(project, catalog, board)
    .filter((d) => d.board === id)
    .flatMap((info): Section[] => {
      const device = project.devices.find((d) => d.id === info.id)!
      const create = isBoardOwn(board, device) ? [] : [requestFrame(createDeviceCall(defs, catalog, boardDeviceId(device), device.type, there(device.config ?? {}) as ProjectDevice['config']).request)]
      const steps = (device.defaults ?? []).map((s) => stepFrame(defs, catalog, there(s) as Step))
      const frames = [...create, ...steps]
      return frames.length === 0 ? [] : [{ id: deviceRecordOf(boardDeviceId(device)), frames }]
    })
}
