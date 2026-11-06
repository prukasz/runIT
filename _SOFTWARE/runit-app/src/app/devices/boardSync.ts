import { eraseProject, listRecords, onboardActionIds, projectInfo, readProjectSource, removeRecord, saveProject, saveProjectSource, serviceCall, storeRecord, vmFrame } from '@backend'
import type { BoardDescriptor, LinkDefinitions, RunitLink, ServiceCatalog } from '@backend'
import type { Engine } from '@engine'
import { parseProject, serializeProject } from '../project/projectFile'
import type { ProjectFile } from '../project/projectFile'
import { settingsRecords } from '../settings/settingsRecords'
import { actionRecordOf, DEVICE_CLASS, deviceProblems, deviceSections, recordClass, SETTINGS_CLASS, stepFrame, USER_CLASS } from './devices'

/*
 * The project's devices and actions on the board, both ways (SYS_PROJECT.MD, SYS_ACTIONS.MD).
 *
 *   write   the old project replaced whole (SYS_PROJECT.MD): the VM stopped (RESET) and the project erased, which removes the
 *           devices it created (the board's stay); then the stored project: the settings the project keeps as their records (settings/settingsRecords.ts), every device record as a section (the board's devices with start commands, then the
 *           project's devices), the program (compiled) and its start (EXEC normal) last, the boot option as the board had it
 *           (run when no project was stored: the board does it right after the save), nothing when there is nothing to store; then every action
 *           as a user record. The device and user records the project no longer has are removed (a project that omits a
 *           device leaves its record in flash, SYS_PROJECT.MD; the firmware's onboard actions stay).
 *   read    the settings and device records the stored project runs (the device records alone when none is stored) and the
 *           user records, back into the project; its program stays.
 */

/** What the board did right after the save, by its boot option. */
const BOOT_NOTE = {
  none: 'The old project\'s devices are removed; the new one waits (At start: nothing; Project, Load or Run).',
  load: 'Loaded: the devices are set up, the program waits to be started.',
  run: 'Loaded and started.',
} as const

export interface SyncProgress {
  readonly step: string
  readonly done: number
  readonly total: number
}

/** The program as the stored project's program section; none when the project has no blocks. */
const programFrames = (defs: LinkDefinitions, engine: Engine, project: ProjectFile): Uint8Array[] | undefined => {
  if (project.program.blocks.length === 0) return undefined
  const result = engine.compile(project.program)
  if (!result.ok) {
    const first = result.diagnostics.find((d) => d.severity === 'error')
    throw new Error(`The program has mistakes${first ? ` (${first.message})` : ''}: fix them in Code first, the board would refuse it.`)
  }
  return result.frames.map((f) => vmFrame(defs, f.op, f.payload))
}

/** Writes the devices, the program and the actions to the board. Resolves with a line for the user. */
export const writeToBoard = async (link: RunitLink, catalog: ServiceCatalog, board: BoardDescriptor, engine: Engine, project: ProjectFile, onProgress?: (p: SyncProgress) => void): Promise<string> => {
  const defs = link.defs
  const problems = deviceProblems(project, catalog, board)
  if (problems.length > 0) throw new Error(problems[0])
  const settings = settingsRecords(catalog, project.settings)
  const sections = [...settings, ...deviceSections(defs, catalog, project, board)]
  const program = programFrames(defs, engine, project)
  const info = await projectInfo(link)
  const boot = info.stored ? info.boot : defs.projectBoot.run
  const start = program ? [vmFrame(defs, engine.exec('normal').op, engine.exec('normal').payload)] : undefined
  // The old program and devices go first: the program would call devices that are gone, and the new run creates them again.
  await link.request(serviceCall(catalog, 'VM', 'RESET').request, { timeoutMs: 10_000, label: 'VM RESET' })
  await eraseProject(link)
  if (sections.length > 0 || program) {
    await saveProject(link, { sections, ...(program ? { program } : {}), ...(start ? { start } : {}) }, { boot, onProgress: (done, total) => onProgress?.({ step: 'Settings, devices and program', done, total }) })
  }

  const onboard = onboardActionIds()
  const ids = await listRecords(link)
  const kept = new Set(sections.map((s) => s.id))
  const staleDevices = ids.filter((id) => recordClass(id) === DEVICE_CLASS && !kept.has(id))
  for (const id of staleDevices) await removeRecord(link, id)
  // A setting the project no longer keeps (the GATT table emptied, a value dropped) must not come back from its old record at the next start.
  for (const id of ids.filter((r) => recordClass(r) === SETTINGS_CLASS && !kept.has(r))) await removeRecord(link, id)
  const stored = ids.filter((id) => recordClass(id) === USER_CLASS && !onboard.has(id & 0xffffff))
  const wanted = new Set(project.actions.map((a) => actionRecordOf(a.id)))
  for (const [i, action] of project.actions.entries()) {
    onProgress?.({ step: 'Actions', done: i, total: project.actions.length })
    if (action.steps.length === 0) continue
    await storeRecord(link, actionRecordOf(action.id), action.steps.map((s) => stepFrame(defs, catalog, s)))
  }
  for (const id of stored) if (!wanted.has(id) || project.actions.find((a) => actionRecordOf(a.id) === id)?.steps.length === 0) await removeRecord(link, id)

  onProgress?.({ step: 'Project', done: 0, total: 1 })
  const after = await projectInfo(link)
  await saveProjectSource(link, { name: project.name, text: serializeProject(project), streamCrc: after.stored ? after.crc : 0 }, (done, total) => onProgress?.({ step: 'Project', done, total }))

  const removed = staleDevices.length > 0 ? ` ${staleDevices.length} device record(s) the project no longer has removed.` : ''
  if (sections.length === 0 && !program) return `Nothing runs at start: the stored project and its devices are erased, ${project.actions.length} action(s) written.${removed}`
  return `Written: ${settings.length} setting record(s), ${sections.length - settings.length} device record(s), ${program ? 'the program, ' : ''}${project.actions.length} action(s). ${BOOT_NOTE[boot === defs.projectBoot.run ? 'run' : boot === defs.projectBoot.load ? 'load' : 'none']}${removed}`
}

/** What reading the board gives: the project it keeps a copy of (none: the board keeps none) and the notes for the user. */
export interface BoardRead {
  readonly project?: ProjectFile
  readonly notes: readonly string[]
}

/** Reads the copy of the project the board keeps. */
export const readFromBoard = async (link: RunitLink, onProgress?: (p: SyncProgress) => void): Promise<BoardRead> => {
  const source = await readProjectSource(link, (done, total) => onProgress?.({ step: 'Project', done, total }))
  if (!source) return { notes: ['The board keeps no copy of a project: nothing read.'] }
  const parsed = parseProject(source.text)
  if (!parsed.ok) throw new Error(`The board's copy of the project is not readable: ${parsed.error}`)
  const info = await projectInfo(link)
  const notes = [`Read the project "${source.name}" from the board.`, ...parsed.problems]
  if ((info.stored ? info.crc : 0) !== source.streamCrc) notes.push('The program stored on the board is not the one this copy was written with: write the project again.')
  return { project: parsed.project, notes }
}
