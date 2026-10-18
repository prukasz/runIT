import { useCallback, useEffect, useRef, useState } from 'react'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { amend, record, redo as redoHistory, startHistory, undo as undoHistory } from './history'
import type { History } from './history'
import { emptyProject, parseProject, PROJECT_EXTENSION, projectFileName, serializeProject } from './projectFile'
import type { ProjectFile } from './projectFile'

/*
 * The open project: its undo history, the file (New, Open, Save) and what the browser keeps of it. The current project is
 * written to browser storage on every change, so a reload or a restart of the app opens it again; New and Open keep the one
 * they replace, and Restore previous swaps it back (so a restore is undone the same way). "Unsaved" compares the project with
 * the text last saved or opened.
 */

const CURRENT_KEY = 'runit.project.current'
const SAVED_KEY = 'runit.project.saved'
const PREVIOUS_KEY = 'runit.project.previous'
/** Edits of one field closer together than this are one undo step. */
const COALESCE_MS = 1500

const readProject = (key: string): ProjectFile | undefined => readStored<ProjectFile | undefined>(key, (raw) => {
  const read = parseProject(raw)
  return read.ok ? read.project : undefined
}, undefined)

const removeStored = (key: string) => {
  try { localStorage.removeItem(key) } catch { /* a convenience only */ }
}

/** What the last file action said: a message, an error, or the mistakes found in an opened project. */
export interface FileNote {
  readonly tone: 'info' | 'error'
  readonly text: string
  readonly problems?: readonly string[]
}

/** The file chooser: a file the user picks, as text; undefined when the chooser is closed. */
const chooseFile = (): Promise<{ name: string; text: string } | undefined> =>
  new Promise((resolve) => {
    const input = document.createElement('input')
    input.type = 'file'
    input.accept = `${PROJECT_EXTENSION},.json,application/json`
    input.addEventListener('change', () => {
      const file = input.files?.[0]
      if (!file) return resolve(undefined)
      file.text().then((text) => resolve({ name: file.name, text }), () => resolve(undefined))
    })
    input.addEventListener('cancel', () => resolve(undefined))
    input.click()
  })

/** Hands the text to the user as a file (the browser's download; the desktop app asks where to save it). */
const download = (name: string, text: string) => {
  const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }))
  const link = document.createElement('a')
  link.href = url
  link.download = name
  link.click()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}

export function useProject() {
  const [history, setHistory] = useState<History<ProjectFile>>(() => startHistory(readProject(CURRENT_KEY) ?? emptyProject()))
  const [savedText, setSavedText] = useState<string | undefined>(() => readStored(SAVED_KEY, (raw) => raw, undefined))
  const [previous, setPrevious] = useState<ProjectFile | undefined>(() => readProject(PREVIOUS_KEY))
  const [note, setNote] = useState<FileNote | undefined>()
  const project = history.present
  const text = serializeProject(project)

  useEffect(() => writeStored(CURRENT_KEY, text), [text])

  const markSaved = (saved: string | undefined) => {
    setSavedText(saved)
    if (saved === undefined) removeStored(SAVED_KEY)
    else writeStored(SAVED_KEY, saved)
  }

  /* The last edit's key and time: edits with the same `key` less than COALESCE_MS apart (typing in one field) are one undo step. */
  const last = useRef<{ key: string; at: number } | undefined>(undefined)

  /** An edit of the project, undone with Undo. Edits with the same `key` in quick succession are undone together. */
  const edit = useCallback((change: (project: ProjectFile) => ProjectFile, key?: string) => {
    const now = Date.now()
    const together = key !== undefined && last.current?.key === key && now - last.current.at < COALESCE_MS
    last.current = key === undefined ? undefined : { key, at: now }
    setHistory((h) => (together ? amend(h, change(h.present)) : record(h, change(h.present))))
  }, [])
  const rename = useCallback((name: string) => edit((p) => (p.name === name ? p : { ...p, name }), 'name'), [edit])
  const undo = useCallback(() => { last.current = undefined; setHistory(undoHistory) }, [])
  const redo = useCallback(() => { last.current = undefined; setHistory(redoHistory) }, [])

  /** Puts `next` in place of the project, keeping the one it replaces for Restore previous. A new history starts. */
  const replace = (next: ProjectFile, saved: string | undefined) => {
    writeStored(PREVIOUS_KEY, serializeProject(project))
    setPrevious(project)
    setHistory(startHistory(next))
    markSaved(saved)
  }

  const newProject = () => {
    replace(emptyProject(), undefined)
    setNote({ tone: 'info', text: 'A new project. The one before can be restored.' })
  }

  const open = async () => {
    const file = await chooseFile()
    if (!file) return
    const read = parseProject(file.text)
    if (!read.ok) return setNote({ tone: 'error', text: `${file.name}: ${read.error}` })
    replace(read.project, serializeProject(read.project))
    setNote(read.problems.length
      ? { tone: 'error', text: `${file.name} opened, with ${read.problems.length} mistake(s) to fix:`, problems: read.problems }
      : { tone: 'info', text: `${file.name} opened.` })
  }

  const save = () => {
    const name = projectFileName(project.name)
    download(name, text)
    markSaved(text)
    setNote({ tone: 'info', text: `Saved as ${name}.` })
  }

  const restorePrevious = () => {
    if (!previous) return
    replace(previous, undefined)
    setNote({ tone: 'info', text: `${previous.name} restored. Restore again to go back.` })
  }

  // Ctrl+Z / Ctrl+Y (and Ctrl+Shift+Z) undo and redo the project; in a text field they stay the field's own.
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (!(event.ctrlKey || event.metaKey) || event.altKey) return
      const target = event.target as HTMLElement | null
      if (target?.closest('input, textarea, [contenteditable="true"]')) return
      const key = event.key.toLowerCase()
      if (key === 'z' && !event.shiftKey) { event.preventDefault(); undo() }
      else if (key === 'y' || (key === 'z' && event.shiftKey)) { event.preventDefault(); redo() }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [undo, redo])

  return {
    project,
    edit,
    rename,
    undo,
    redo,
    canUndo: history.past.length > 0,
    canRedo: history.future.length > 0,
    /** Changed since it was last saved or opened (a new project counts as unsaved). */
    unsaved: savedText !== text,
    newProject,
    open: () => void open(),
    save,
    previous,
    restorePrevious,
    note,
    clearNote: () => setNote(undefined),
  }
}
