/**
 * Sends a widget's values to the board without flooding it: per variable at most one write is in flight and
 * one is sent per interval, and a value that arrives meanwhile replaces the one waiting (a stick sends the
 * newest position, not every position it passed).
 */
export const createWriter = (
  send: (variableId: string, value: number) => Promise<void>,
  onError: (failure: unknown) => void,
  intervalMs = 50,
  now: () => number = Date.now,
): ((variableId: string, value: number) => void) => {
  interface Entry { pending?: number; busy: boolean; last: number; timer?: ReturnType<typeof setTimeout> }
  const entries = new Map<string, Entry>()
  const flush = (id: string, entry: Entry): void => {
    if (entry.busy || entry.pending === undefined) return
    const wait = entry.last + intervalMs - now()
    if (wait > 0) {
      entry.timer ??= setTimeout(() => { entry.timer = undefined; flush(id, entry) }, wait)
      return
    }
    const value = entry.pending
    entry.pending = undefined
    entry.busy = true
    entry.last = now()
    send(id, value).catch(onError).finally(() => { entry.busy = false; flush(id, entry) })
  }
  return (id, value) => {
    let entry = entries.get(id)
    if (!entry) {
      entry = { busy: false, last: -Infinity }
      entries.set(id, entry)
    }
    entry.pending = value
    flush(id, entry)
  }
}
