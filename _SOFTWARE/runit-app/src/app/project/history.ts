/*
 * Undo and redo: the states before (`past`), the current one, and the ones undone (`future`). A new edit drops the future.
 * Pure, so a test can hold it; the project keeps one history (useProject.ts).
 */

export interface History<T> {
  readonly past: readonly T[]
  readonly present: T
  readonly future: readonly T[]
}

/** How many edits can be undone. */
export const HISTORY_LIMIT = 100

export const startHistory = <T>(present: T): History<T> => ({ past: [], present, future: [] })

/** An edit: `next` becomes the present; the same object again is no edit. */
export const record = <T>(history: History<T>, next: T, limit = HISTORY_LIMIT): History<T> =>
  next === history.present ? history : { past: [...history.past, history.present].slice(-limit), present: next, future: [] }

/** An edit that continues the last one (typing in the same field): it replaces the present and is undone together with it. */
export const amend = <T>(history: History<T>, next: T): History<T> =>
  next === history.present ? history : { ...history, present: next, future: [] }

export const undo = <T>(history: History<T>): History<T> =>
  history.past.length === 0 ? history : { past: history.past.slice(0, -1), present: history.past[history.past.length - 1]!, future: [history.present, ...history.future] }

export const redo = <T>(history: History<T>): History<T> =>
  history.future.length === 0 ? history : { past: [...history.past, history.present], present: history.future[0]!, future: history.future.slice(1) }
