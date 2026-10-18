import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { createEngine } from '@engine'
import type { ExecCommand } from '@engine'
import { Services, bundledEnums, leaveSafeState } from '@backend'
import type { LinkState, RunitLink } from '@backend'

/** How often the board is asked how its program runs, while connected. */
const STATUS_EVERY_MS = 1000

/** What `VM STATUS` answers (`vm_wire_status_t`, VM_CORE.MD "Remote"). */
export interface BoardRun {
  /** `vm_run_mode_e` by its name (`VM_RUN_RUNNING`, ...). */
  readonly mode: string
  /** A program is loaded and complete: EXEC can start it. */
  readonly ready: boolean
  readonly faultLatched: boolean
  readonly passes: number
}

/** The `vm_run_mode_e` member name of a number, from the published enums (never a number typed in here). */
const runModeNames = (): ReadonlyMap<number, string> => new Map(bundledEnums().vm_run_mode_e?.members.map((member) => [member.value, member.name]) ?? [])

/**
 * Run, Stop and the safe stop, and what the board says of its program.
 *
 * While connected the app asks `VM STATUS` every second: the run mode, whether a program is loaded and complete, the fault
 * latch. With a ready program on the board, Run and Stop are real: Run leaves the safe state and starts the program
 * (`leaveSafeState` with `EXEC normal`, or `EXEC resume` from a pause), Stop pauses it (`EXEC pause`). With no program on the
 * board (or a firmware without STATUS) they are a simulation that sends nothing. The safe stop is always real: `SYS STOP`,
 * the board's safe state (the VM stopped, every device output switched off and suspended).
 */
export function useVm(link: RunitLink, linkState: LinkState) {
  const engine = useMemo(() => createEngine(), [])
  const services = useMemo(() => new Services(link), [link])
  const names = useMemo(runModeNames, [])
  const [board, setBoard] = useState<BoardRun | undefined>()
  const [simulated, setSimulated] = useState(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | undefined>()
  const connected = linkState === 'connected'
  const supported = useRef(true)

  const refresh = useCallback(async () => {
    if (!supported.current) return
    try {
      const { value } = await services.call('VM', 'STATUS', undefined, { timeoutMs: 1500, label: 'VM STATUS (poll)' })
      const status = value as Record<string, unknown>
      setBoard({
        mode: names.get(Number(status.mode)) ?? `mode ${String(status.mode)}`,
        ready: status.ready === true,
        faultLatched: status.fault_latched === true,
        passes: Number(status.pass_count),
      })
    } catch (failure) {
      // A firmware without STATUS refuses it: the app falls back to the simulation and stops asking.
      if (failure instanceof Error && /device-error|unknown/i.test(`${(failure as { code?: string }).code ?? ''} ${failure.message}`)) supported.current = false
      setBoard(undefined)
    }
  }, [services, names])

  useEffect(() => {
    setBoard(undefined)
    setSimulated(false)
    setError(undefined)
    supported.current = true
    if (!connected) return
    void refresh()
    const timer = setInterval(() => void refresh(), STATUS_EVERY_MS)
    return () => clearInterval(timer)
  }, [connected, refresh])

  const real = board?.ready === true
  const running = real ? board.mode === 'VM_RUN_RUNNING' : simulated

  const act = useCallback(async (command: () => Promise<unknown>) => {
    setBusy(true)
    setError(undefined)
    try {
      await command()
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure))
    } finally {
      setBusy(false)
      await refresh()
    }
  }, [refresh])

  const exec = useCallback((command: ExecCommand) => {
    const frame = engine.exec(command)
    return link.request({ keys: [services.catalog.service('VM').key!, frame.op], payload: frame.payload }, { label: `VM EXEC ${command}` })
  }, [engine, link, services])

  const run = useCallback(() => {
    if (!real) return setSimulated(true)
    return act(() => (board.mode === 'VM_RUN_FROZEN' ? exec('resume') : leaveSafeState(link, { start: engine.exec('normal') })))
  }, [real, board, act, exec, link, engine])
  const stop = useCallback(() => {
    if (!real) return setSimulated(false)
    return act(() => exec('pause'))
  }, [real, act, exec])
  const safeStop = useCallback(() => act(async () => {
    await services.call('SYS', 'STOP')
    setSimulated(false)
  }), [act, services])

  return {
    /** What the board answered last; undefined while unknown (not connected, or no STATUS). */
    board,
    /** Run and Stop act on the board's program (a ready one is loaded); otherwise they are a simulation. */
    real,
    running,
    error,
    canRun: connected && !busy && !running,
    canStop: !busy && running,
    canSafeStop: connected && !busy,
    run,
    stop,
    safeStop,
  }
}
