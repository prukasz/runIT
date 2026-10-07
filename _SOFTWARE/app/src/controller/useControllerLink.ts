import { useEffect, useMemo, useRef, useState } from 'react'
import { useDebug } from '../debug/DebugContext'
import { linkedVariables } from '../domain/controller'
import type { Controller } from '../domain/controller'
import { createWriter } from './liveWrite'
import { numberOfLive } from './variables'
import type { WidgetIo } from './widgets'

/**
 * The widgets' link to the board: the debug session. Enabling the interface uploads the program and runs it
 * (the debug build, which also reports the variables), the widgets then read the reported values and write
 * theirs. Everything stays off while the interface is not enabled.
 */
export function useControllerLink(controller: Controller) {
  const debug = useDebug()
  const live = debug.active && !debug.stale
  const [failure, setFailure] = useState<string>()
  const latest = useRef(debug)
  latest.current = debug
  const send = useMemo(() => createWriter(
    (id, value) => latest.current.write(id, value),
    (cause) => setFailure(cause instanceof Error ? cause.message : String(cause)),
  ), [])

  // The variables the widgets use are reported by the board once the interface runs.
  const used = linkedVariables(controller).all
  const key = used.join('|')
  useEffect(() => {
    if (!live) return
    for (const id of used) if (latest.current.watchState(id) === 'off') latest.current.watch(id)
    // `used` follows `key`; the session object changes on every telemetry refresh and must not re-run this.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [live, key])
  useEffect(() => { if (!live) setFailure(undefined) }, [live])

  const io: WidgetIo = {
    live,
    read: (id) => (id && live ? numberOfLive(debug.read(id)) : undefined),
    write: (id, value) => { if (id && live) send(id, value) },
  }
  return {
    io,
    debug,
    live,
    problem: debug.error ?? failure,
    clearProblem: () => { debug.clearError(); setFailure(undefined) },
  }
}
