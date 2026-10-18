import { checkVariables, flattenVariables } from '@engine'
import type { Project } from '@engine'
import type { FieldValues } from '@backend'

/*
 * The project as the user keeps it: a name, the program (the engine's variables and blocks), the devices the project makes
 * on the board with the commands each gets at start, the start commands of the board's own devices, the user's actions, the
 * user's names of pins and the board's settings the project keeps (settings/settings.ts). Plain data, saved as one JSON file. Reading a file never throws: a file that is not a project is an error
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
export interface ProjectDevice {
  readonly id: number
  readonly type: string
  readonly name: string
  readonly config?: FieldValues
  readonly defaults?: readonly Step[]
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
}

/** A GATT service the project adds (BLE SVC_CREATE); the board's own service is the firmware's. */
export interface GattService {
  readonly uuid: number
  readonly primary: boolean
  readonly characteristics: readonly GattCharacteristic[]
}

/** Where a data stream is sent: a transport (`transport_e`) and its endpoint (a characteristic's UUID; 0 on the serial port). */
export interface StreamBinding {
  readonly provider: number
  readonly endpoint: number
}

/** A data stream (connector): the board's own (logs, errors, telemetry, events) or the project's. */
export interface DataStream {
  readonly id: number
  readonly name: string
  readonly stream: number
  readonly system: boolean
  readonly suspended: boolean
  readonly bindings: readonly StreamBinding[]
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
}

export const emptyProject = (name = 'Untitled'): ProjectFile => ({ name, program: { variables: [], blocks: [] }, devices: [], boardDefaults: [], actions: [], pinAliases: [], settings: emptySettings() })

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
  const project: ProjectFile = {
    name: typeof data.name === 'string' && data.name.trim() ? data.name : 'Untitled',
    program: program as unknown as Project,
    devices: devices as unknown as ProjectDevice[],
    boardDefaults: boardDefaults as unknown as BoardDefaults[],
    actions: actions as unknown as ProjectAction[],
    pinAliases: pinAliases as unknown as PinAlias[],
    settings: { values: {}, tables: {}, ...(settings as unknown as Partial<ProjectSettings>) },
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
