import { useCallback, useEffect, useMemo, useState } from 'react'
import { bundledServiceCatalog } from '@backend'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { amend, record, redo as redoHistory, startHistory, undo as undoHistory } from '../project/history'
import type { History } from '../project/history'
import { parseProject, serializeProject } from '../project/projectFile'
import { chooseFile, download } from '../project/useProject'
import type { ProjectFile } from '../project/projectFile'
import { boardFromProfile, profileFromBoard, profileKey, withBoard } from './boardEditor'
import { bundledBoard } from './boardDescriptor'
import type { BoardKind } from './boardDescriptor'

/*
 * The board's profile in the app (boardEditor.ts): the devices of the board and their boot packages, kept in the app's own storage
 * and written to the board (SYS_BOARD.MD). It starts as the bundled board's devices. Edited like a project's devices, with its own
 * undo; `board` is the descriptor the rest of the app uses.
 */

const COALESCE_MS = 1500

/** A board profile's file: a project file whose devices are the board's (devices/boardEditor.ts). */
export const PROFILE_EXTENSION = '.runit-board.json'

/** What `useDevices` edits: a project file and the change of it. */
export interface ProfileState {
  readonly project: ProjectFile
  readonly edit: (change: (project: ProjectFile) => ProjectFile, key?: string) => void
}

/** The incomplete 7-device snapshot that shipped before the rest of the PCB's chip descriptors existed. */
const LEGACY_PCB_IDS = '0,1,3,10,11,14,15'

const isLegacyIncompletePcb = (stored: ProjectFile, base: ReturnType<typeof bundledBoard>): boolean =>
  stored.name === base.name
  && stored.devices.map((d) => d.id).sort((a, b) => a - b).join(',') === LEGACY_PCB_IDS
  && base.devices.length > stored.devices.length

export function useBoardProfile() {
  const base = useMemo(() => bundledBoard(), [])
  const catalog = useMemo(bundledServiceCatalog, [])
  const key = profileKey('pcb')
  const [history, setHistory] = useState<History<ProjectFile>>(() => {
    const stored = readStored<ProjectFile | undefined>(key, (raw) => { const read = parseProject(raw); return read.ok ? read.project : undefined }, undefined)
    // Upgrade the legacy 7-device PCB profile automatically to the full rev 1 board profile (all 15 chips).
    const initial = !stored || isLegacyIncompletePcb(stored, base)
      ? profileFromBoard(base, catalog)
      : withBoard(stored, base)
    return startHistory(initial)
  })
  const project = history.present
  const text = useMemo(() => serializeProject(project), [project])
  useEffect(() => writeStored(key, text), [key, text])

  const last = useMemo(() => ({ key: undefined as string | undefined, at: 0 }), [])
  const edit = useCallback((change: (project: ProjectFile) => ProjectFile, editKey?: string) => {
    const now = Date.now()
    const together = editKey !== undefined && last.key === editKey && now - last.at < COALESCE_MS
    last.key = editKey
    last.at = now
    setHistory((h) => (together ? amend(h, change(h.present)) : record(h, change(h.present))))
  }, [last])
  const undo = useCallback(() => { last.key = undefined; setHistory(undoHistory) }, [last])

  /** What the last Open or Save said. */
  const [note, setNote] = useState<{ readonly tone: 'info' | 'error'; readonly text: string } | undefined>()
  /** Opens a profile file (`runit-app/profiles/` has the bundled boards'): it takes this one's place; Undo goes back. Write puts it on the board. */
  const open = useCallback(async () => {
    const file = await chooseFile(`${PROFILE_EXTENSION},.json,application/json`)
    if (!file) return
    const read = parseProject(file.text)
    if (!read.ok) return setNote({ tone: 'error', text: `${file.name}: ${read.error}` })
    last.key = undefined
    setHistory((h) => record(h, withBoard(read.project, base)))
    setNote({ tone: 'info', text: `${file.name} opened: Write puts it on the board.` })
  }, [last, base])
  /** Saves the profile as a file. */
  const save = useCallback(() => {
    const name = `${project.name.trim().replace(/[\\/:*?"<>|]+/g, '-') || 'board'}${PROFILE_EXTENSION}`
    download(name, serializeProject(project))
    setNote({ tone: 'info', text: `Saved as ${name}.` })
  }, [project])
  const loadPreset = useCallback((kind: BoardKind) => {
    const preset = profileFromBoard(bundledBoard(kind), catalog)
    last.key = undefined
    setHistory((h) => record(h, preset))
    const label = kind === 'pcb' ? 'runIT PCB (r1)' : 'ESP32-S3 DevKit'
    setNote({ tone: 'info', text: `Loaded ${label} profile (${preset.devices.length} devices): Write puts it on the board.` })
  }, [catalog, last])

  const redo = useCallback(() => { last.key = undefined; setHistory(redoHistory) }, [last])

  /** The descriptor the app works with: the bundled board's static parts and the profile's devices. */
  const board = useMemo(() => boardFromProfile(project, base, catalog), [project, base, catalog])
  /** The board without any device: the static parts alone (the editor's own view of the board). */
  const bare = useMemo(() => ({ ...board, devices: [] }), [board])
  return { project, edit, open: () => void open(), save, loadPreset, note, clearNote: () => setNote(undefined), undo, redo, canUndo: history.past.length > 0, canRedo: history.future.length > 0, board, bare, base }
}

export type BoardProfileState = ReturnType<typeof useBoardProfile>
