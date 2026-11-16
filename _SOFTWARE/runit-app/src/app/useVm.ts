import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { ExecCommand } from '@engine'
import { Services, Watches, bundledEnums, serviceCall } from '@backend'
import type { LinkState, RunitLink } from '@backend'
import type { Project } from '@engine'
import { LiveValues } from './run/live'
import type { LiveView, RunMode } from './run/live'

/** How often the board sends its safe state, while connected (a watch, SYS_WATCH.MD). */
const STATUS_EVERY_MS = 1000

/** How slow the board's passes are in a debug run (`VM SPEED`, 1 is normal): what a person can follow with the eye. */
export const SLOW_FACTORS = [1, 2, 5, 10, 20] as const

/** The board's own program time per stepped pass, ms (`vm_clock.c`): what a run that is not a debug leaves it at. */
export const STEP_MS_BOARD = 10

/** How the VM goes through its passes: all the time, one pass per request, one block per request. */
export type Stepping = 'continuous' | 'loop' | 'step'

/** What `VM STATUS` answers (`vm_wire_status_t`, VM_CORE.MD "Remote"). */
export interface BoardRun {
  /** `vm_run_mode_e` by its name (`VM_RUN_RUNNING`, ...). */
  readonly mode: string
  /** A program is loaded and complete: EXEC can start it. */
  readonly ready: boolean
  readonly faultLatched: boolean
  /** The device of the latched fault (meaningful only while latched). */
  readonly faultDevice?: number
  readonly passes: number
  /** The board's slow-motion factor (`VM SPEED`, 1 is normal). */
  readonly slowdown: number
  /** The block (its place in the program) the VM is parked before while it is stepped by block; undefined when it is not parked. */
  readonly nextBlock?: number
}

/** The board's safe state as `SYS SAFE` answers it: why it entered it (`sys_safe_reason_e`) and since when. */
export interface SafeState {
  /** The reason's member name (`SYS_SAFE_POWER`). */
  readonly reason: string
  /** As the user reads it, and what it means (the enum's alias and description). */
  readonly alias: string
  readonly description?: string
  /** How long the board has been in it, when it was read. */
  readonly forMs: number
}

/** The safe state's reasons (`sys_safe_reason_e`), by value and by name. */
const safeReasons = () => bundledEnums().sys_safe_reason_e?.members ?? []

/**
 * The board's safe state and the safe stop. The program's Run, Pause, debug and live view are off: the firmware has no VM yet, so
 * there is nothing to load a program into (`canRun`, `canWatch` and `canStop` are false, `real` is false), and the members that
 * would run it stay so the screens do not change; they come back with the VM.
 *
 * While connected the board sends `SYS SAFE` every second (a watch: one request and no more): whether it is in its safe state,
 * why (STOP, the dead-man, a lost link, a power fault) and since when. `leaveSafe` is the way out (`SYS SAFE_LEAVE`: the devices
 * resumed), the safe stop is `SYS STOP` (every device output switched off and suspended).
 */
export function useVm(link: RunitLink, linkState: LinkState, _program: Project, holdMs: number) {
  const services = useMemo(() => new Services(link), [link])
  const [safe, setSafe] = useState<SafeState | undefined>()
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | undefined>()
  const values = useRef(new LiveValues(holdMs))
  const connected = linkState === 'connected'

  /** `SYS SAFE`'s answer, from a request or a watch. */
  const applySafe = useCallback((value: unknown) => {
    if (value === undefined || value === null || typeof value !== 'object') {
      setSafe(undefined)
      return
    }
    const answer = value as Record<string, unknown>
    const member = safeReasons().find((m) => m.value === Number(answer.reason) || m.name === answer.reason)
    setSafe(member ? { reason: member.name, alias: member.alias ?? member.name, ...(member.description ? { description: member.description } : {}), forMs: Number(answer.for_ms) } : undefined)
  }, [])

  const refresh = useCallback(async () => {
    try {
      const { value } = await services.call('SYS', 'SAFE', undefined, { timeoutMs: 1500, label: 'SYS SAFE' })
      applySafe(value)
    } catch {
      setSafe(undefined)
    }
  }, [services, applySafe])

  useEffect(() => {
    setSafe(undefined)
    setError(undefined)
    if (!connected) return
    void refresh()
    // The board sends the answer by itself from here on; the watch is added again when the link comes back.
    const watches = new Watches(link)
    let stopped = false
    let stop: (() => Promise<void>) | undefined
    watches
      .follow({ ...serviceCall(services.catalog, 'SYS', 'SAFE', undefined, 'read'), label: 'SYS SAFE (watch)' }, { trigger: 'every', periodMs: STATUS_EVERY_MS }, (value) => {
        if (!stopped) applySafe(value)
      })
      .then((done) => void (stop = done))
      .catch(() => undefined) // a link without watches: the one-shot refresh stands
    return () => {
      stopped = true
      void stop?.().catch(() => undefined)
      watches.close()
    }
  }, [connected, refresh, link, services, applySafe])

  values.current.holdMs = holdMs

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

  /** Out of the safe state: the devices resumed. */
  const leaveSafe = useCallback(() => act(() => services.call('SYS', 'SAFE_LEAVE')), [act, services])
  const safeStop = useCallback(() => act(() => services.call('SYS', 'STOP')), [act, services])

  // Nothing the VM would show: no value is live, no block acts.
  const live = useMemo<LiveView>(() => ({
    mode: undefined,
    dropped: 0,
    next: undefined,
    active: () => false,
    changed: () => false,
    flag: () => undefined,
    text: () => undefined,
  }), [])

  const off = useCallback(async (): Promise<void> => undefined, [])
  const noRun = useCallback((_mode?: RunMode, _slow?: number, _how?: Stepping, _stepMs?: number): Promise<void> => off(), [off])
  const noControl = useCallback((_command: Extract<ExecCommand, 'normal' | 'scan' | 'block' | 'once' | 'next' | 'restart'>): Promise<void> | undefined => undefined, [])
  const noSlow = useCallback((_factor: number, _stepMs: number): Promise<void> | undefined => undefined, [])

  return {
    /** What the board answered of its program: nothing, it has no VM. */
    board: undefined as BoardRun | undefined,
    /** The board's safe state while it is in it (`SYS SAFE`); undefined otherwise, or while unknown. */
    safe,
    leaveSafe,
    ackFault: off,
    /** Run and Stop act on a program on the board: never, there is no VM. */
    real: false,
    running: false,
    error,
    /** While a program is being loaded: how far. */
    progress: undefined as string | undefined,
    loaded: undefined as { mode: RunMode; slow: number } | undefined,
    stale: false,
    live,
    canRun: false,
    canWatch: false,
    canStop: false,
    canSafeStop: connected && !busy,
    run: noRun,
    watchLive: off,
    control: noControl,
    slowTo: noSlow,
    stepping: undefined as Stepping | undefined,
    stop: off,
    safeStop,
  }
}
