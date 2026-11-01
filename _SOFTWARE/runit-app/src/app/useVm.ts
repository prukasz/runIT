import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { createEngine } from '@engine'
import type { CompileResult, ExecCommand, Project, TelemetryReader, WatchPlan } from '@engine'
import { Services, Watches, bundledEnums, leaveSafeState, loadProgram, sendProgram, serviceCall } from '@backend'
import type { LinkState, RunitLink } from '@backend'
import { LiveValues, liveText } from './run/live'
import type { LiveView, RunMode } from './run/live'

/** How often the board sends how its program runs and its safe state, while connected (two watches, SYS_WATCH.MD). */
const STATUS_EVERY_MS = 1000

/** How slow the board's passes are in a debug run (`VM SPEED`, 1 is normal): what a person can follow with the eye. */
export const SLOW_FACTORS = [1, 2, 5, 10, 20] as const

/** The board's own program time per stepped pass, ms (`vm_clock.c`): what a run that is not a debug leaves it at. */
export const STEP_MS_BOARD = 10

/** How the VM goes through its passes: all the time, one pass per request (`VM_RUN_SCAN`), one block per request (`VM_RUN_BLOCK`). */
export type Stepping = 'continuous' | 'loop' | 'step'
const STEPPING_START: Record<Stepping, ExecCommand> = { continuous: 'normal', loop: 'scan', step: 'block' }
/** The run modes (`vm_run_mode_e`) in which a program is started: it runs, or waits for the next pass / block. */
const STARTED: Record<string, Stepping> = { VM_RUN_RUNNING: 'continuous', VM_RUN_SCAN: 'loop', VM_RUN_STEP: 'loop', VM_RUN_BLOCK: 'step', VM_RUN_BLOCK_STEP: 'step' }

/** Not seen more often than this: telemetry can arrive on every pass, a screen needs about this many redraws a second. */
const REDRAW_EVERY_MS = 60

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

/** The `vm_run_mode_e` member name of a number, from the published enums (never a number typed in here). */
const runModeNames = (): ReadonlyMap<number, string> => new Map(bundledEnums().vm_run_mode_e?.members.map((member) => [member.value, member.name]) ?? [])

/** The safe state's reasons (`sys_safe_reason_e`), by value and by name. */
const safeReasons = () => bundledEnums().sys_safe_reason_e?.members ?? []

/**
 * Run, Stop and the safe stop, and what the board says of its program.
 *
 * While connected the board sends `VM STATUS` every second (a watch: one request and no more): the run mode, whether a program is loaded and complete, the fault
 * latch. With a ready program on the board, Run and Stop are real: Run leaves the safe state and starts the program
 * (`leaveSafeState` with `EXEC normal`, or `EXEC resume` from a pause), Stop pauses it (`EXEC pause`). With no program on the
 * board (or a firmware without STATUS) they are a simulation that sends nothing. The safe stop is always real: `SYS STOP`,
 * the board's safe state (the VM stopped, every device output switched off and suspended).
 *
 * It sends `SYS SAFE` every second too: whether the board is in its safe state, why (STOP, the dead-man, a lost link, a power
 * fault) and since when. `leaveSafe` is the way out (`leaveSafeState`: the devices resumed, the program started again when one is
 * ready), `ackFault` clears a latched device fault (`EXEC ackFault`) that refuses every start.
 */
export function useVm(link: RunitLink, linkState: LinkState, program: Project, holdMs: number) {
  const engine = useMemo(() => createEngine(), [])
  const services = useMemo(() => new Services(link), [link])
  const names = useMemo(runModeNames, [])
  const [board, setBoard] = useState<BoardRun | undefined>()
  const [safe, setSafe] = useState<SafeState | undefined>()
  const safeSupported = useRef(true)
  const [simulated, setSimulated] = useState(false)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | undefined>()
  const [progress, setProgress] = useState<string | undefined>()
  /** What this session loaded into the board and how it was started; the program of the project is `stale` when it has changed since. */
  const [loaded, setLoaded] = useState<{ program: Project; mode: RunMode; slow: number } | undefined>()
  const [version, setVersion] = useState(0)
  /** The stepping this session last selected on the board (`EXEC normal`, `scan`, `block`). */
  const [selected, setSelected] = useState<Stepping>('continuous')
  const watch = useRef<{ result: CompileResult; reader: TelemetryReader; plan: WatchPlan; mode: RunMode } | undefined>(undefined)
  const values = useRef(new LiveValues(holdMs))
  const redraw = useRef<{ throttle?: ReturnType<typeof setTimeout>; expiry?: ReturnType<typeof setTimeout> }>({})
  const connected = linkState === 'connected'
  const supported = useRef(true)

  /** `VM STATUS`'s answer, from a request or a watch; undefined when the board could not answer. */
  const applyStatus = useCallback((value: unknown) => {
    if (value === undefined || value === null || typeof value !== 'object') {
      setBoard(undefined)
      return
    }
    const status = value as Record<string, unknown>
    setBoard({
      mode: names.get(Number(status.mode)) ?? `mode ${String(status.mode)}`,
      ready: status.ready === true,
      faultLatched: status.fault_latched === true,
      ...(status.fault_latched === true && status.fault_device !== undefined ? { faultDevice: Number(status.fault_device) } : {}),
      passes: Number(status.pass_count),
      slowdown: Number(status.slowdown ?? 1),
      ...(status.next_block !== undefined && Number(status.next_block) !== 0xffff ? { nextBlock: Number(status.next_block) } : {}),
    })
  }, [names])

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
    if (!supported.current) return
    try {
      const { value } = await services.call('VM', 'STATUS', undefined, { timeoutMs: 1500, label: 'VM STATUS' })
      applyStatus(value)
    } catch (failure) {
      // A firmware without STATUS refuses it: the app falls back to the simulation and stops asking.
      if (failure instanceof Error && /device-error|unknown/i.test(`${(failure as { code?: string }).code ?? ''} ${failure.message}`)) supported.current = false
      setBoard(undefined)
    }
    if (!safeSupported.current) return
    try {
      const { value } = await services.call('SYS', 'SAFE', undefined, { timeoutMs: 1500, label: 'SYS SAFE' })
      applySafe(value)
    } catch (failure) {
      // A firmware without SAFE refuses it: the app stops asking and shows no safe state.
      if (failure instanceof Error && /device-error|unknown/i.test(`${(failure as { code?: string }).code ?? ''} ${failure.message}`)) safeSupported.current = false
      setSafe(undefined)
    }
  }, [services, names])

  useEffect(() => {
    setBoard(undefined)
    setSafe(undefined)
    setSimulated(false)
    setError(undefined)
    supported.current = true
    safeSupported.current = true
    if (!connected) return
    void refresh()
    // The board sends the two answers by itself from here on; the watches are added again when the link comes back.
    const watches = new Watches(link)
    const stops: (() => Promise<void>)[] = []
    let stopped = false
    const follow = (service: string, command: string, apply: (value: unknown) => void) =>
      watches
        .follow({ ...serviceCall(services.catalog, service, command, undefined, 'read'), label: `${service} ${command} (watch)` }, { trigger: 'every', periodMs: STATUS_EVERY_MS }, (value) => {
          if (!stopped) apply(value)
        })
        .then((stop) => void stops.push(stop))
        .catch(() => undefined) // a firmware without watches: the one-shot refresh stands
    void follow('VM', 'STATUS', applyStatus)
    void follow('SYS', 'SAFE', applySafe)
    return () => {
      stopped = true
      for (const stop of stops) void stop().catch(() => undefined)
      watches.close()
    }
  }, [connected, refresh, link, services, applyStatus, applySafe])

  values.current.holdMs = holdMs

  /** Redraws the screens that show the live values: soon, and again when the oldest pulse stops showing. */
  const tick = useCallback(() => {
    clearTimeout(redraw.current.expiry)
    redraw.current.expiry = undefined
    setVersion((v) => v + 1)
    const wait = values.current.nextExpiry(performance.now())
    if (wait !== undefined) redraw.current.expiry = setTimeout(tick, wait + 10)
  }, [])
  const schedule = useCallback(() => {
    if (redraw.current.throttle !== undefined) return
    redraw.current.throttle = setTimeout(() => {
      redraw.current.throttle = undefined
      tick()
    }, REDRAW_EVERY_MS)
  }, [tick])

  useEffect(() => link.on('telemetry', (data) => {
    const w = watch.current
    const update = w?.reader.read(data)
    if (w && update && values.current.apply(update, w.plan, performance.now())) schedule()
  }), [link, schedule])

  // The board's subscription outlives a dropped link, but the page does not know that: ask again when the link is back, which
  // sends everything once.
  useEffect(() => {
    const w = watch.current
    if (connected && w) void sendProgram(link, [engine.subscribe(w.plan.ids)]).catch(() => undefined)
  }, [connected, link, engine])

  const stopWatching = useCallback(() => {
    watch.current = undefined
    values.current.clear()
    tick()
  }, [tick])
  useEffect(() => () => { clearTimeout(redraw.current.throttle); clearTimeout(redraw.current.expiry) }, [])

  const real = board?.ready === true
  // After a requested pass the board is FROZEN (VM_CORE.MD, vm_exec.c: ONCE runs a pass, then freezes with the scan mode still selected),
  // which says nothing of a pause: the app knows which mode it selected (`selected`), so a frozen program in a step mode is still the
  // started one, waiting for the next request.
  const waiting = real && board.mode === 'VM_RUN_FROZEN' && selected !== 'continuous' && loaded !== undefined
  const running = real ? board.mode in STARTED || waiting : simulated
  /** How the started program goes through its passes now. */
  const stepping: Stepping | undefined = real ? STARTED[board.mode] ?? (waiting ? selected : undefined) : undefined

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
    return link.request({ keys: [services.catalog.service('VM').key!, services.catalog.dirs.write, frame.op], payload: frame.payload }, { label: `VM EXEC ${command}` })
  }, [engine, link, services])

  /** What is wrong with a program that will not compile, as one line for the bar. */
  const refusal = (result: CompileResult): string => {
    const errors = result.diagnostics.filter((d) => d.severity === 'error')
    return `The program has ${errors.length} ${errors.length === 1 ? 'mistake' : 'mistakes'}, fix ${errors.length === 1 ? 'it' : 'them'} first: ${errors[0]?.message ?? 'it does not compile'}`
  }

  /**
   * Run, in one of three modes. The program of the project is compiled and loaded into the board (`loadProgram`: from OPEN, each
   * frame answered, again from OPEN when the link fails), the telemetry asked for (live and debug: the blocks' When done, the
   * variables and the block outputs; debug at the slow-motion factor), then the program started (`leaveSafeState` with `EXEC
   * normal`, so it also leaves a safe stop). A program that is paused and is the one loaded, in the same mode, is only resumed.
   * Every mode compiles with `allEnableOuts`, so the program Play loads is the one the live view watches (`watchLive`).
   */
  const run = useCallback((mode: RunMode = 'run', slow = 1, how: Stepping = 'continuous', stepMs = STEP_MS_BOARD) => {
    if (!connected) return
    // Play on a paused program is Resume, whatever way it was started (a paused debug stays a debug); the eye and the bug ask for a way.
    if (real && board.mode === 'VM_RUN_FROZEN' && loaded?.program === program && (mode === 'run' || (loaded.mode === mode && loaded.slow === slow))) return act(() => exec('resume'))
    return act(async () => {
      if (program.blocks.length === 0 && program.variables.length === 0) throw new Error('The program is empty: add a block or a variable first.')
      const result = engine.compile(program, { allEnableOuts: true })
      if (!result.ok) throw new Error(refusal(result))
      // Nothing from an earlier program may be read as this one's.
      stopWatching()
      setProgress('Loading the program…')
      try {
        await loadProgram(link, result.frames, { onProgress: (done, total) => setProgress(`Loading the program ${done}/${total}`) })
        const plan = mode === 'run' ? undefined : engine.watch(result, mode === 'debug' ? 'full' : 'flow')
        if (plan) watch.current = { result, reader: engine.telemetry(result), plan, mode }
        await sendProgram(link, [...(plan ? [engine.subscribe(plan.ids)] : []), engine.speed(mode === 'debug' ? slow : 1, mode === 'debug' ? stepMs : STEP_MS_BOARD)])
        await leaveSafeState(link, { start: engine.exec(STEPPING_START[mode === 'debug' ? how : 'continuous']) })
      } finally {
        setProgress(undefined)
      }
      setSelected(mode === 'debug' ? how : 'continuous')
      setLoaded({ program, mode, slow })
      setSimulated(true)
    })
  }, [connected, real, board, loaded, program, act, exec, engine, link, stopWatching])
  /**
   * The eye. On the running program this session loaded, unchanged since: watch it as it runs, by asking for its telemetry
   * (`SUBSCRIBE`), nothing loaded again; pressed again, stop watching (an empty subscription) and let it run. Otherwise (nothing
   * loaded here, or the project changed) load it and run it watched.
   */
  const watchLive = useCallback(() => {
    if (!connected) return
    if (watch.current?.mode === 'live') {
      stopWatching()
      return act(() => sendProgram(link, [engine.subscribe([])]))
    }
    if (!(running && loaded !== undefined && loaded.program === program && real)) return run('live')
    return act(async () => {
      const result = engine.compile(program, { allEnableOuts: true })
      if (!result.ok) throw new Error(refusal(result))
      stopWatching()
      const plan = engine.watch(result, 'flow')
      watch.current = { result, reader: engine.telemetry(result), plan, mode: 'live' }
      await sendProgram(link, [engine.subscribe(plan.ids)])
    })
  }, [connected, running, loaded, program, real, run, act, engine, link, stopWatching])
  /** Debug controls on the started program: change how it steps (`normal`, `scan`, `block`), go on by one pass (`once`) or one block (`next`), back to the start (`restart`). */
  const control = useCallback((command: Extract<ExecCommand, 'normal' | 'scan' | 'block' | 'once' | 'next' | 'restart'>) => (real ? act(async () => {
    // The VM is still on its way to the next stop when the command is answered: ask again shortly for which block it parks before.
    setTimeout(() => void refresh(), 150)
    await exec(command)
    // Back to the start of a program that runs on its own: the board freezes there, and a frozen program would look stopped.
    if (command === 'restart' && selected === 'continuous') await exec('resume')
    if (command === 'normal' || command === 'scan' || command === 'block') setSelected(command === 'normal' ? 'continuous' : command === 'scan' ? 'loop' : 'step')
  }) : undefined), [real, act, exec, selected, refresh])
  /** Slow motion on the board now (`VM SPEED`). */
  const slowTo = useCallback((factor: number, stepMs: number) => (real ? act(() => sendProgram(link, [engine.speed(factor, stepMs)])) : undefined), [real, act, link, engine])
  const stop = useCallback(() => {
    if (!real) return setSimulated(false)
    return act(() => exec('pause'))
  }, [real, act, exec])
  /** Out of the safe state: the devices resumed, and the program started again when a ready one is loaded and no fault is latched. */
  const leaveSafe = useCallback(() => act(() => leaveSafeState(link, real && !board?.faultLatched ? { start: engine.exec('normal') } : {})), [act, link, real, board, engine])
  const ackFault = useCallback(() => act(() => exec('ackFault')), [act, exec])
  const safeStop = useCallback(() => act(async () => {
    await services.call('SYS', 'STOP')
    setSimulated(false)
  }), [act, services])

  const stale = running && loaded !== undefined && loaded.program !== program
  const watched = connected && real && watch.current !== undefined
  // `version` is what makes this a new object, so a screen that reads it redraws when a value arrived or a pulse ended.
  const live = useMemo<LiveView>(() => {
    const now = () => performance.now()
    const cells = values.current
    return {
      mode: watched ? watch.current!.mode : undefined,
      dropped: watched ? watch.current!.plan.dropped : 0,
      next: watched && board?.nextBlock !== undefined ? watch.current!.result.order[board.nextBlock] : undefined,
      active: (key) => watched && cells.active(key, now()),
      changed: (key) => watched && cells.changed(key, now()),
      flag: (key) => (watched ? cells.flag(key, now()) : undefined),
      text: (key) => {
        const value = watched ? cells.value(key) : undefined
        return value === undefined ? undefined : liveText(value)
      },
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [version, watched, board?.nextBlock])

  return {
    /** What the board answered last; undefined while unknown (not connected, or no STATUS). */
    board,
    /** The board's safe state while it is in it (`SYS SAFE`); undefined otherwise, or while unknown. */
    safe,
    leaveSafe,
    ackFault,
    /** Run and Stop act on the board's program (a ready one is loaded); otherwise they are a simulation. */
    real,
    running,
    error,
    /** While a program is being loaded: how far. */
    progress,
    /** What is loaded was started by this session: how, and whether the project's program has changed since. */
    loaded: loaded && { mode: loaded.mode, slow: loaded.slow },
    stale,
    /** What the board says of the program that runs, for the screens (the `LiveContext`). */
    live,
    canRun: connected && !busy && (!running || stale),
    /** The eye and the bug start a watched run: any time the link is up, but not again for the same live view of an unchanged program. */
    canWatch: connected && !busy,
    canStop: !busy && running,
    canSafeStop: connected && !busy,
    run,
    watchLive,
    control,
    slowTo,
    stepping,
    stop,
    safeStop,
  }
}
