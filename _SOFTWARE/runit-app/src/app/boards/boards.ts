import { createDeviceCall, requestFrame } from '@backend'
import type { LinkDefinitions, ServiceCatalog } from '@backend'
import type { ProjectBoard, ProjectDevice, ProjectFile, Step } from '../project/projectFile'
import { boardDeviceId, configPins, deviceList, deviceRecordOf, stepFrame } from '../devices/devices'
import type { Section } from '../devices/devices'
import type { BoardDescriptor } from '../devices/boardDescriptor'

/*
 * The other boards of the project, pure (no link, no UI): boards the project leads, each with its id, its address and the
 * provider of its link, and the devices a project puts on them.
 *
 *   devices   a project device on another board (`ProjectDevice.board`) is created there with its config and start commands, its
 *             ids those of that board (`boardSections`), and is an alias on this board (devices.ts `remoteFrame`).
 *
 * Finding a board (a BLE scan) and linking it (CONNECT, the data matrix's role and route) are not here: the firmware has no BLE
 * central yet.
 */

/** The service commands this file sends (SYS descriptors). */
export const BOARD_COMMANDS = { system: 'SYS', restart: 'RESTART' } as const

/** The first provider id the app gives a link: above the board's own (1..4) and the ports and characteristics a project makes (0x10..). */
export const FIRST_LINK_PROVIDER = 0x30
/** The last one: the UART devices' providers begin at 0x40. */
export const LAST_LINK_PROVIDER = 0x3f

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

/* ---- the links ---- */

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
      const create = isBoardOwn(board, device) ? [] : [requestFrame(createDeviceCall(catalog, boardDeviceId(device), device.type, there(device.config ?? {}) as ProjectDevice['config']).request)]
      const steps = (device.defaults ?? []).map((s) => stepFrame(defs, catalog, there(s) as Step))
      const frames = [...create, ...steps]
      return frames.length === 0 ? [] : [{ id: deviceRecordOf(boardDeviceId(device)), frames }]
    })
}
