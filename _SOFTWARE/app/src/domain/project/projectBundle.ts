import type { ProjectCanvas } from '../canvas'
import { actionsFromJson, devicesFromJson, stepsFromJson } from '../devices'
import type { ActionStep, ProjectAction, ProjectDevice } from '../devices'
import { boardConnectors, configFromJson, connectorsFromJson, defaultBleProfile, defaultConfig, profileFromJson, settingsOps } from '../settings'
import type { BleProfile, ConfigValues, ConnectorSettings } from '../settings'
import { createProject } from './document'
import type { ProjectDocument } from './document'
import { parseProject, ProjectFormatError, serializeProject } from './projectFile'

/*
 * The whole project as one file (`runit-project`): the object tree (projectFile.ts), the canvases and the settings of the Project
 * tab (BLE profile, data connectors, and the values of every `@settings` op by router). Download writes it, Open reads it back;
 * a part the file lacks starts from its defaults, so a file written before that part existed still opens.
 */

/** The values of the settings ops of each router, by router symbol and op symbol. */
export type RouterSettingsValues = Readonly<Record<string, Readonly<Record<string, ConfigValues>>>>

export interface ProjectSettingsFile {
  readonly ble: BleProfile
  readonly connectors: readonly ConnectorSettings[]
  readonly routers: RouterSettingsValues
}

/** The user's devices, and the names the project gives the board's own (by device ID). */
export interface ProjectDevicesFile {
  readonly list: readonly ProjectDevice[]
  readonly boardNames: Readonly<Record<string, string>>
  readonly pinAliases?: Readonly<Record<string, Readonly<Record<string, string>>>>
  /** The default settings of the devices, and the user's actions: commands the board keeps as records. */
  readonly setup: readonly ActionStep[]
  readonly actions: readonly ProjectAction[]
}

export interface ProjectBundle {
  readonly project: ProjectDocument
  readonly canvases: readonly ProjectCanvas[]
  readonly devices: ProjectDevicesFile
  readonly settings: ProjectSettingsFile
}

/** The routers whose `@settings` ops the project keeps. */
export const SETTINGS_ROUTERS = ['ROUTER_KEY_LOG', 'ROUTER_KEY_POWER'] as const

const defaultRouters = (): RouterSettingsValues =>
  Object.fromEntries(SETTINGS_ROUTERS.map((router) => [router, Object.fromEntries(settingsOps(router).map((op) => [op.symbol, defaultConfig(op)]))]))

/** An empty project: no objects, no canvases (the editor adds the first), the board's default settings. */
export const emptyBundle = (name = 'Untitled'): ProjectBundle => ({
  project: createProject(name),
  canvases: [],
  devices: { list: [], boardNames: {}, setup: [], actions: [] },
  settings: { ble: defaultBleProfile(), connectors: boardConnectors(), routers: defaultRouters() },
})

export const serializeBundle = (bundle: ProjectBundle): string => {
  const document = JSON.parse(serializeProject(bundle.project)) as object
  return `${JSON.stringify({ ...document, canvases: bundle.canvases, devices: bundle.devices, settings: bundle.settings }, null, 2)}\n`
}

const routersFrom = (raw: unknown): RouterSettingsValues => {
  const saved = typeof raw === 'object' && raw !== null && !Array.isArray(raw) ? (raw as Record<string, unknown>) : {}
  return Object.fromEntries(SETTINGS_ROUTERS.map((router) => {
    const ops = typeof saved[router] === 'object' && saved[router] !== null ? (saved[router] as Record<string, unknown>) : {}
    return [router, Object.fromEntries(settingsOps(router).map((op) => [op.symbol, configFromJson(op, JSON.stringify(ops[op.symbol] ?? {})) ?? defaultConfig(op)]))]
  }))
}

/** Reads a project file; throws `ProjectFormatError` naming what is wrong. */
export const parseBundle = (text: string): ProjectBundle => {
  const project = parseProject(text)
  const doc = JSON.parse(text) as Record<string, unknown>
  if (doc.canvases !== undefined && !Array.isArray(doc.canvases)) throw new ProjectFormatError('canvases', 'expected a list')
  const settings = typeof doc.settings === 'object' && doc.settings !== null ? (doc.settings as Record<string, unknown>) : {}
  const devices = typeof doc.devices === 'object' && doc.devices !== null ? (doc.devices as Record<string, unknown>) : {}
  const boardNames = typeof devices.boardNames === 'object' && devices.boardNames !== null ? Object.fromEntries(Object.entries(devices.boardNames).filter((entry): entry is [string, string] => typeof entry[1] === 'string')) : {}
  const rawPinAliases = typeof devices.pinAliases === 'object' && devices.pinAliases !== null ? (devices.pinAliases as Record<string, unknown>) : undefined
  const pinAliases = rawPinAliases
    ? Object.fromEntries(
        Object.entries(rawPinAliases).map(([devRef, pMap]) => [
          devRef,
          typeof pMap === 'object' && pMap !== null
            ? Object.fromEntries(Object.entries(pMap).filter((entry): entry is [string, string] => typeof entry[1] === 'string'))
            : {},
        ]),
      )
    : undefined
  return {
    project,
    canvases: (doc.canvases as ProjectCanvas[] | undefined) ?? [],
    devices: {
      list: devicesFromJson(devices.list),
      boardNames,
      ...(pinAliases ? { pinAliases } : {}),
      setup: stepsFromJson(devices.setup),
      actions: actionsFromJson(devices.actions),
    },
    settings: {
      ble: (settings.ble !== undefined && profileFromJson(JSON.stringify(settings.ble))) || defaultBleProfile(),
      connectors: (settings.connectors !== undefined && connectorsFromJson(JSON.stringify(settings.connectors))) || boardConnectors(),
      routers: routersFrom(settings.routers),
    },
  }
}

