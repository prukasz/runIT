import { checkVariables, flattenVariables } from '@engine'
import type { PowerReading } from '../power/powerDisplay'
import type { Project } from '@engine'
import type { BoardPower, FieldValues } from '@backend'
import { EMPTY_LAYOUT, readLayout } from '../code/canvas/layout'
import type { CanvasLayout } from '../code/canvas/layout'
import { readHardware } from '../hardware/layout'
import type { HardwareLayout } from '../hardware/layout'

/*
 * The project as the user keeps it: a name, the program (the engine's variables and blocks), the devices the project makes
 * on the board with the commands each gets at start, the start commands of the board's own devices, the user's actions, the
 * user's names of pins, the board's settings the project keeps (settings/settings.ts) and where the blocks sit on the canvas (the
 * app's own: the board never gets it, code/canvas/layout.ts). Plain data, saved as one JSON file. Reading a file never throws: a file that is not a project is an error
 * message, a project with mistakes in it opens and lists them (a mistake in a project is a diagnostic, AGENTS.md).
 */

export const PROJECT_FORMAT = 'runit-project'
export const PROJECT_VERSION = 1
/** The extension a saved project gets. */
export const PROJECT_EXTENSION = '.runit.json'

/**
 * One command of a record: a device command as the catalog names it (a contract's or the device type's own, with its
 * arguments by field id), or a frame the catalog does not describe, kept as its bytes.
 */
export type Step =
  | { readonly kind: 'device'; readonly device: number; readonly contract: string; readonly command: string; readonly args?: FieldValues }
  | { readonly kind: 'frame'; readonly bytes: readonly number[] }

/**
 * A device the project creates on the board (`Services.create`): its id there, its type (a device descriptor's id), its name, its
 * config, and the commands it gets right after it is created (`defaults`). Stored as the device's record (SYS_ACTIONS.MD).
 */
/** An I2C bus the board creates at every load, kept in the board's profile (`devices/boardEditor.ts`): its pins are pins of device `sda.device`. */
export interface ProfileI2cBus {
  readonly bus: number
  readonly name: string
  readonly sda: { readonly device: number; readonly pin: number }
  readonly scl: { readonly device: number; readonly pin: number }
  readonly pullup: boolean
  readonly description?: string
}

/** A pin of the board nothing may take (the board's profile): which device's, what it is and why. */
export interface ProfileReservedPin {
  readonly device: number
  readonly pin: number
  readonly label: string
  readonly reason?: string
}

export interface ProjectDevice {
  readonly id: number
  readonly type: string
  readonly name: string
  readonly config?: FieldValues
  readonly defaults?: readonly Step[]
  /**
   * The board it is on, when not the one the app is connected to: a board of `ProjectFile.boards` (its id). It is created there
   * with its config and start commands, and on this board `id` is its alias (DEVICE CREATE_REMOTE, SYS_REMOTE.MD), which the
   * program uses like any device. Absent: this board.
   */
  readonly board?: number
  /** Its id on `board` (absent: `id`); a device of that board's own (its GPIO, 0) is not created there, only set up. */
  readonly remoteId?: number
}

/** The start commands of one of the board's own devices (board.json): it is created by the firmware, so its record has no CREATE. */
export interface BoardDefaults {
  readonly device: number
  readonly steps: readonly Step[]
}

/** The user's name of a pin of a device (`LED blue` for pin 22 of the GPIO expander): shown wherever the app names that pin. */
export interface PinAlias {
  readonly device: number
  readonly pin: number
  readonly name: string
}

/**
 * Another board of the project, linked to this one over BLE (SYS_BLE.MD "Central"): this board keeps a link to it as provider
 * `provider` and a session with it as its slave; the app reaches it through this board (`dst` = `id`).
 */
export interface ProjectBoard {
  /** Its board id (`SYS_SESSION_ID`), given by the app: other than this board's. */
  readonly id: number
  readonly name: string
  /** Its BLE address as it is shown, most significant byte first (`F4:12:FA:01:02:03`). */
  readonly address: string
  /** 0 public, 1 random. */
  readonly addressType: number
  /** The id of this board's provider that is the link to it. */
  readonly provider: number
}

/** A user action: a list of commands stored as a record of the user class, run by an app, the VM or an error policy. */
export interface ProjectAction {
  /** The record's index in the user class (the record ID). */
  readonly id: number
  readonly name: string
  readonly steps: readonly Step[]
}

/** One row of a settings table (the power response matrix): its target (the board or a device) and a value per column. */
export interface SettingsRow {
  readonly target: number
  readonly cells: readonly number[]
}

/** A GATT characteristic of a user service (BLE CHAR_CREATE). */
export interface GattCharacteristic {
  readonly uuid: number
  readonly name: string
  readonly write: boolean
  readonly indicate: boolean
  readonly notify: boolean
  readonly framed: boolean
  readonly tx: number
  readonly rx: number
  /** The id of its provider, what a stream binds to; absent: none. Only an unframed characteristic has one. */
  readonly provider?: number
}

/** A GATT service the project adds (BLE SVC_CREATE); the board's own service is the firmware's. */
export interface GattService {
  readonly uuid: number
  readonly primary: boolean
  readonly characteristics: readonly GattCharacteristic[]
}

/** Where a data stream is sent: a provider, by its id. */
export interface StreamBinding {
  readonly provider: number
}

/** A stream's target on another board: `provider` of `board`. */
export interface StreamRemote {
  readonly board: number
  readonly provider: number
}

/** A data stream (SYS_STREAM.MD): the board's own (logs, errors, telemetry, events) or the project's. */
export interface DataStream {
  /** Its id on the board: the byte that starts each of its frames (the board's own 0x01..0x0F, the project's 0x10..0x7F). */
  readonly stream: number
  /** The project's label of its own stream; the board keeps none (the board's four are named by the group `stream`). */
  readonly name?: string
  readonly system: boolean
  /** OUT: the board sends it to its bindings; IN: what comes in on its bindings. Set when it is made. Absent: OUT. */
  readonly dir?: 'out' | 'in'
  readonly bindings: readonly StreamBinding[]
  /** An IN stream's ways out: the ids of the providers what comes in goes to. Absent: none. */
  readonly outputs?: readonly number[]
  /** Providers of other boards (or a client's receivers) it goes to. Absent: none. */
  readonly remotes?: readonly StreamRemote[]
}

/**
 * The board's settings the project keeps: each is written as its settings record (a section of the stored project, so it is
 * applied at every start). A setting left out is not written: the board keeps its own.
 */
export interface ProjectSettings {
  /** The values of a setting form, by its key (`LOG.SET`). */
  readonly values: Readonly<Record<string, FieldValues>>
  /** The rows of a settings table, by its key (`POWER_MANAGER.SET_RESPONSES`). */
  readonly tables: Readonly<Record<string, readonly SettingsRow[]>>
  /** The user's GATT services. */
  readonly gatt?: readonly GattService[]
  /** Every data stream, the board's and the user's. */
  readonly streams?: readonly DataStream[]
}

export const emptySettings = (): ProjectSettings => ({ values: {}, tables: {} })

export interface ProjectFile {
  readonly name: string
  readonly program: Project
  readonly devices: readonly ProjectDevice[]
  readonly boardDefaults: readonly BoardDefaults[]
  readonly actions: readonly ProjectAction[]
  readonly pinAliases: readonly PinAlias[]
  readonly settings: ProjectSettings
  /** Where the blocks and the free chips sit on the canvas. */
  readonly canvas: CanvasLayout
  /** What the Live power display reads (`power/powerDisplay.ts`); unset: the board's default. The app's own, like the canvas. */
  readonly powerDisplay?: readonly PowerReading[]
  /**
   * The Devices canvas (`hardware/layout.ts`): the parts round the board, their wires, the power choices, the project's own parts.
   * The app's own; what it decides is in `devices` and `boardDefaults` (`hardware/apply.ts`). Unset: nothing placed yet.
   */
  readonly hardware?: HardwareLayout
  /** The other boards of the project, linked to this one over BLE. Absent: none. */
  readonly boards?: readonly ProjectBoard[]
  /** The board's profile only: the I2C buses it creates at every load. Absent: the bundled board's. */
  readonly i2cBuses?: readonly ProfileI2cBus[]
  /** The board's profile only: the UART ports it creates at every load, each `UART CREATE`'s arguments. Absent: none. */
  readonly uartPorts?: readonly FieldValues[]
  /** The board's profile only: the pins nothing may take (strapping pins, USB), with why. The buses' and ports' pins are reserved by them. */
  readonly reservedPins?: readonly ProfileReservedPin[]
  /** The board's profile only: its power (limits, the input's and the rails' monitors, the USB-PD sink, the source pins). */
  readonly power?: BoardPower
}

export const emptyProject = (name = 'Untitled'): ProjectFile => ({ name, program: { variables: [], blocks: [] }, devices: [], boardDefaults: [], actions: [], pinAliases: [], settings: emptySettings(), canvas: EMPTY_LAYOUT })

export const serializeProject = (project: ProjectFile): string =>
  JSON.stringify({ format: PROJECT_FORMAT, version: PROJECT_VERSION, ...project }, null, 2)

export type ParseResult =
  | { readonly ok: true; readonly project: ProjectFile; readonly problems: readonly string[] }
  | { readonly ok: false; readonly error: string }

const isObject = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value)

export const parseProject = (text: string): ParseResult => {
  let data: unknown
  try {
    data = JSON.parse(text)
  } catch {
    return { ok: false, error: 'The file is not a project: it is not JSON.' }
  }
  if (!isObject(data) || data.format !== PROJECT_FORMAT) return { ok: false, error: 'The file is not a runIT project.' }
  if (typeof data.version !== 'number' || data.version > PROJECT_VERSION) {
    return { ok: false, error: `The project was saved by a newer app (format version ${String(data.version)}); this one reads up to ${PROJECT_VERSION}.` }
  }
  const program = data.program
  if (!isObject(program) || !Array.isArray(program.variables) || !Array.isArray(program.blocks)) {
    return { ok: false, error: 'The project has no program (variables and blocks).' }
  }
  const devices = data.devices ?? []
  if (!Array.isArray(devices) || !devices.every((d) => isObject(d) && Number.isInteger(d.id) && typeof d.type === 'string' && typeof d.name === 'string')) {
    return { ok: false, error: 'The devices of the project are not readable (each needs an id, a type and a name).' }
  }
  const boardDefaults = data.boardDefaults ?? []
  if (!Array.isArray(boardDefaults) || !boardDefaults.every((d) => isObject(d) && Number.isInteger(d.device) && Array.isArray(d.steps))) {
    return { ok: false, error: "The start commands of the board's devices are not readable (each needs a device and its steps)." }
  }
  const actions = data.actions ?? []
  if (!Array.isArray(actions) || !actions.every((a) => isObject(a) && Number.isInteger(a.id) && typeof a.name === 'string' && Array.isArray(a.steps))) {
    return { ok: false, error: 'The actions of the project are not readable (each needs an id, a name and its steps).' }
  }
  const pinAliases = data.pinAliases ?? []
  if (!Array.isArray(pinAliases) || !pinAliases.every((a) => isObject(a) && Number.isInteger(a.device) && Number.isInteger(a.pin) && typeof a.name === 'string')) {
    return { ok: false, error: 'The pin names of the project are not readable (each needs a device, a pin and a name).' }
  }
  const settings = data.settings ?? emptySettings()
  if (!isObject(settings) || !isObject(settings.values ?? {}) || !isObject(settings.tables ?? {})
    || (settings.gatt !== undefined && !Array.isArray(settings.gatt)) || (settings.streams !== undefined && !Array.isArray(settings.streams))) {
    return { ok: false, error: "The board settings of the project are not readable." }
  }
  const boards = data.boards ?? []
  if (!Array.isArray(boards) || !boards.every((b) => isObject(b) && Number.isInteger(b.id) && typeof b.name === 'string' && typeof b.address === 'string' && Number.isInteger(b.addressType) && Number.isInteger(b.provider))) {
    return { ok: false, error: 'The other boards of the project are not readable (each needs an id, a name, an address and a provider).' }
  }
  const project: ProjectFile = {
    name: typeof data.name === 'string' && data.name.trim() ? data.name : 'Untitled',
    program: program as unknown as Project,
    devices: devices as unknown as ProjectDevice[],
    boardDefaults: boardDefaults as unknown as BoardDefaults[],
    actions: actions as unknown as ProjectAction[],
    pinAliases: pinAliases as unknown as PinAlias[],
    settings: { values: {}, tables: {}, ...(settings as unknown as Partial<ProjectSettings>) },
    canvas: readLayout(data.canvas),
    // A display that is not readable is left out: the board's default stands in.
    ...(Array.isArray(data.powerDisplay) && data.powerDisplay.every((r) => isObject(r) && typeof r.name === 'string' && isObject(r.read)) ? { powerDisplay: data.powerDisplay as unknown as PowerReading[] } : {}),
    ...(data.hardware !== undefined ? { hardware: readHardware(data.hardware) } : {}),
    ...(boards.length > 0 ? { boards: boards as unknown as ProjectBoard[] } : {}),
    ...(Array.isArray(data.i2cBuses) && data.i2cBuses.every((b) => isObject(b) && Number.isInteger(b.bus) && isObject(b.sda) && isObject(b.scl)) ? { i2cBuses: data.i2cBuses as unknown as ProfileI2cBus[] } : {}),
    ...(Array.isArray(data.uartPorts) && data.uartPorts.every(isObject) ? { uartPorts: data.uartPorts as unknown as FieldValues[] } : {}),
    ...(Array.isArray(data.reservedPins) && data.reservedPins.every((r) => isObject(r) && Number.isInteger(r.device) && Number.isInteger(r.pin) && typeof r.label === 'string') ? { reservedPins: data.reservedPins as unknown as ProfileReservedPin[] } : {}),
    ...(isObject(data.power) && isObject(data.power.limits) && Array.isArray(data.power.rails) ? { power: data.power as unknown as BoardPower } : {}),
  }
  return { ok: true, project, problems: checkVariables(project.program.variables).map((problem) => problem.message) }
}

/** What the project holds, for a summary. */
export const projectCounts = (project: ProjectFile) => ({
  variables: flattenVariables(project.program.variables).filter((v) => v.kind !== 'folder').length,
  blocks: project.program.blocks.length,
  devices: project.devices.length,
  actions: project.actions.length,
})

/** A file name from the project's name. */
export const projectFileName = (name: string): string =>
  `${name.trim().replace(/[\\/:*?"<>|]+/g, '-').replace(/\s+/g, ' ') || 'project'}${PROJECT_EXTENSION}`
