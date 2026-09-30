import { useRef, useState } from 'react'

export interface HistoryState<T> {
  readonly past: readonly T[]
  readonly present: T
  readonly future: readonly T[]
}

interface EditOptions {
  key?: string
  discrete?: boolean
}

/** Undo transitions shared by workspaces; persistence and edit validation stay with each view. */
export function useUndoHistory<T>(initial: () => T, options: { limit?: number; coalesceMs?: number } = {}) {
  const [state, setState] = useState<HistoryState<T>>(() => ({ past: [], present: initial(), future: [] }))
  const stateRef = useRef(state)
  const lastEdit = useRef<{ time: number; key?: string }>({ time: 0 })
  const limit = options.limit ?? 50

  const commit = (next: HistoryState<T>) => {
    stateRef.current = next
    setState(next)
  }
  const trim = (past: readonly T[]) => past.slice(-limit)
  const current = () => stateRef.current.present

  const record = (next: T, editOptions: EditOptions = {}): boolean => {
    const before = stateRef.current
    if (next === before.present) return false
    const now = Date.now()
    const coalesced = !editOptions.discrete && editOptions.key !== undefined &&
      editOptions.key === lastEdit.current.key && now - lastEdit.current.time < (options.coalesceMs ?? 0) && before.past.length > 0
    lastEdit.current = { time: now, key: editOptions.key }
    commit({ past: coalesced ? before.past : trim([...before.past, before.present]), present: next, future: [] })
    return true
  }

  const replace = (next: T, mode: 'undoable' | 'reset' = 'undoable') => {
    const before = stateRef.current
    lastEdit.current = { time: 0 }
    commit({ past: mode === 'reset' ? [] : trim([...before.past, before.present]), present: next, future: [] })
  }

  const undo = (): boolean => {
    lastEdit.current = { time: 0 }
    const before = stateRef.current
    if (!before.past.length) return false
    commit({ past: before.past.slice(0, -1), present: before.past.at(-1)!, future: [before.present, ...before.future] })
    return true
  }

  const redo = (): boolean => {
    lastEdit.current = { time: 0 }
    const before = stateRef.current
    if (!before.future.length) return false
    commit({ past: trim([...before.past, before.present]), present: before.future[0]!, future: before.future.slice(1) })
    return true
  }

  return { present: state.present, current, record, replace, undo, redo, canUndo: state.past.length > 0, canRedo: state.future.length > 0 }
}
