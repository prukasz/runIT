import { deviceCall, listRecords, onboardActionIds, PROVIDER_COMMANDS, removeRecord, runRecord, Services, storeRecord } from '@backend'
import type { RunitLink, ServiceCatalog } from '@backend'
import type { ProjectFile } from '../project/projectFile'
import { bleProviders, gattRemoves } from '../settings/gatt'
import { settingsRecords } from '../settings/settingsRecords'
import { actionRecordOf, DEVICE_CLASS, DEVICE_COMMANDS, deviceProblems, deviceSections, lifecycleOf, recordClass, SETTINGS_CLASS, stepFrame, USER_CLASS } from './devices'
import { boardProblems } from '../boards/boards'
import type { BoardDescriptor } from './boardDescriptor'

/*
 * The project's devices and actions on the board (SYS_ACTIONS.MD). The board keeps no project of its own yet: the project stays in
 * the app, and what the board gets are records.
 *
 *   write   the devices the project made before go (the registry's REMOVE sweep leaves the board's own), then the settings the
 *           project keeps as their records (settings/settingsRecords.ts) and every device record (the board's devices with start
 *           commands, then the project's devices) are stored and run in that order, the project's actions are stored as user
 *           records. The device and settings records the project no longer has are removed, so they do not come back; the
 *           firmware's onboard actions stay.
 *   read    nothing: the board keeps no copy of the project (its profile has its own tab, boardProfile.ts).
 *
 * Nothing runs by itself at a boot: the board's profile is the one record the firmware runs at start (SYS_BOARD.MD), so the
 * project's devices are made again by the next Write.
 */

export interface SyncProgress {
  readonly step: string
  readonly done: number
  readonly total: number
}

/** Writes the devices, the settings and the actions to the board. Resolves with a line for the user. */
export const writeToBoard = async (link: RunitLink, catalog: ServiceCatalog, board: BoardDescriptor, project: ProjectFile, onProgress?: (p: SyncProgress) => void): Promise<string> => {
  const defs = link.defs
  const problems = [...deviceProblems(project, catalog, board), ...boardProblems(project, link.session?.boardId, catalog)]
  if (problems.length > 0) throw new Error(problems[0])
  const settings = settingsRecords(catalog, project.settings, defs)
  const sections = [...settings, ...deviceSections(defs, catalog, project, board)]

  // The devices of the last Write go first: its CREATEs would find them there. The board's own stay (SYS_DEVICE_CREATE_SYSTEM).
  onProgress?.({ step: 'Devices', done: 0, total: sections.length })
  await link.request(deviceCall(catalog, catalog.allDevices!, lifecycleOf(catalog).id, DEVICE_COMMANDS.remove, undefined, 'write').request, { timeoutMs: 10_000, label: 'devices REMOVE (sweep)' })

  // The providers the project's BLE record creates may be on the board from the last Write: they go first.
  if (project.settings.gatt?.length) {
    const have = bleProviders(catalog, (await new Services(link, catalog).call(PROVIDER_COMMANDS.service, PROVIDER_COMMANDS.list)).data)
    for (const call of gattRemoves(catalog, have, project.settings.gatt)) await link.request(call.request, { label: 'BLE provider REMOVE' })
  }

  const ids = await listRecords(link, catalog)
  const kept = new Set(sections.map((s) => s.id))
  for (const section of sections) await storeRecord(link, catalog, section.id, section.frames)
  // A record the project no longer keeps (a device, a setting dropped) must not come back from its old record.
  const stale = ids.filter((id) => (recordClass(id) === DEVICE_CLASS || recordClass(id) === SETTINGS_CLASS) && !kept.has(id))
  for (const id of stale) await removeRecord(link, catalog, id)
  for (const [i, section] of sections.entries()) {
    onProgress?.({ step: 'Settings and devices', done: i, total: sections.length })
    await runRecord(link, catalog, section.id, true)
  }

  const onboard = onboardActionIds()
  const stored = ids.filter((id) => recordClass(id) === USER_CLASS && !onboard.has(id & 0xffffff))
  const wanted = new Set(project.actions.map((a) => actionRecordOf(a.id)))
  for (const [i, action] of project.actions.entries()) {
    onProgress?.({ step: 'Actions', done: i, total: project.actions.length })
    if (action.steps.length === 0) continue
    await storeRecord(link, catalog, actionRecordOf(action.id), action.steps.map((s) => stepFrame(defs, catalog, s)))
  }
  for (const id of stored) if (!wanted.has(id) || project.actions.find((a) => actionRecordOf(a.id) === id)?.steps.length === 0) await removeRecord(link, catalog, id)

  const removed = stale.length > 0 ? ` ${stale.length} record(s) the project no longer has removed.` : ''
  return `Written and run: ${settings.length} setting record(s), ${sections.length - settings.length} device record(s), ${project.actions.length} action(s). They are not run at a start: Write again after a restart.${removed}`
}

/** What reading the board gives: the project it keeps a copy of (none: the board keeps none) and the notes for the user. */
export interface BoardRead {
  readonly project?: ProjectFile
  readonly notes: readonly string[]
}

/** The board keeps no copy of the project: the project is the app's. */
export const readFromBoard = async (): Promise<BoardRead> => ({ notes: ['The board keeps no copy of the project: it stays in this app, Write puts its devices on the board.'] })
