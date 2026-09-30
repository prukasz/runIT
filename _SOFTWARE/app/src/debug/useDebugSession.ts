import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { packExec, packSubscribe, pathWires, readPath } from '../domain/compiler'
import type { LiveValues } from '../domain/compiler'
import { applyVmValues, decodeBoardFrame, decodeVmTelemetry } from '../domain/decoder'
import { runitErrorCatalog, runitStreamCatalog, runitValueNames, runitVmCatalog } from '../domain/descriptors'
import { serializeProject } from '../domain/project'
import type { ObjectPath, ObjectSection, ProjectDocument } from '../domain/project'
import { blockLive, nextBlockIndex, planVmUpload, subscribeRoom, withWatched } from '../domain/upload'
import type { BlockLive, VmUploadPlan } from '../domain/upload'
import type { RunitBleSession } from '../backend/runitBleSession'
import { DEFAULT_MAX_FRAME_BYTES } from '../frameLimits'
import { useDebugHold } from './debugPrefs'
import { describeCommandError, sendSteps } from '../sendSteps'
import type { DebugView, RunAction, RunMode, WatchState } from './DebugContext'

/*
 * Debug mode: upload the debug build of the canvases (every block with an ENO
 * object, subscriptions on everything the blocks read, write and are gated by),
 * run it, and keep the latest telemetry for the canvas to draw. Telemetry is
 * gathered as it arrives and handed to React ten times a second.
 */

const catalog = runitVmCatalog()
const catalogs = { streams: runitStreamCatalog(), errors: runitErrorCatalog(), names: runitValueNames() }
/** State refresh: telemetry can come faster than a canvas needs to redraw. */
const FLUSH_MS = 100
/** A failed block stays marked this long after its last error packet. */
const FAILED_MS = 2000

interface Running {
  readonly session: RunitBleSession
  readonly plan: VmUploadPlan
  /** Project + sections as uploaded, to tell when the editor moved on. */
  readonly source: string
}

export const useDebugSession = (project: ProjectDocument, sections: readonly ObjectSection[], session: RunitBleSession | undefined): DebugView => {
  const [started, setStarted] = useState<Running>()
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string>()
  const [live, setLive] = useState<LiveValues>(new Map())
  const [now, setNow] = useState(0)
  const store = useRef<LiveValues>(new Map())
  const dirty = useRef(false)
  const failedAt = useRef(new Map<string, number>())
  /**
   * When each ENO / enable-source wire was last seen true, kept for the hold
   * time (settings): a tick, and a block acting on it, are true for one pass,
   * so the latest value is nearly always the 0 after it.
   */
  const trueAt = useRef(new Map<number, number>())
  const holdMs = useDebugHold()
  const hold = useRef(holdMs)
  hold.current = holdMs
  const [mode, setMode] = useState<RunMode>('running')
  const [next, setNext] = useState<number>()
  const [extra, setExtra] = useState<ReadonlySet<string>>(new Set())
  const [notice, setNotice] = useState<string>()
  const resumeMode = useRef<RunMode>('running')
  const commanding = useRef(false)
  /** The subscription last sent, to send only a change. */
  const sentKey = useRef('')
  const source = useMemo(() => JSON.stringify([serializeProject(project), sections]), [project, sections])
  // A new connection (or none) knows nothing of the debug build.
  const running = started && started.session === session ? started : undefined

  const stop = useCallback(() => {
    setStarted(undefined)
    store.current = new Map()
    failedAt.current = new Map()
    trueAt.current = new Map()
    dirty.current = false
    setLive(new Map())
  }, [])

  useEffect(() => {
    if (started && !running) stop()
  }, [started, running, stop])

  useEffect(() => {
    if (!running) return undefined
    const { session: link, plan } = running
    const sizeOf = (id: number) => {
      const placed = plan.program.objects.objects[id]
      return placed && { bytes: placed.elements * placed.type.wireWidth, wireWidth: placed.type.wireWidth }
    }
    // The wires whose true moments are held: each block's ENO and what gates it.
    const pulses = new Set<number>()
    for (const watch of plan.debug?.blocks.values() ?? []) {
      if (watch.eno !== undefined) pulses.add(watch.eno)
      for (const path of watch.enables) for (const wire of pathWires(plan.program.objects, path)) pulses.add(wire)
    }
    const unsubscribe = link.received.subscribe((frame) => {
      try {
        const decoded = decodeVmTelemetry(frame.data, catalog)
        if (decoded) {
          if (decoded.kind === 'values') {
            store.current = applyVmValues(store.current, decoded.records, sizeOf)
            const time = Date.now()
            if (hold.current > 0) for (const record of decoded.records) if (pulses.has(record.id) && record.data.some((byte) => byte !== 0)) trueAt.current.set(record.id, time)
            dirty.current = true
          }
          return
        }
        const board = decodeBoardFrame(frame.data, catalogs)
        if (board.kind !== 'errors') return
        for (const node of board.report.nodes) {
          if (node.tag?.name !== 'ERR_VM_BLOCK_FAILED') continue
          const block = plan.program.blocks.blocks[Number(node.fields.block_idx)]
          if (block) failedAt.current.set(block.id, Date.now())
        }
        dirty.current = true
      } catch {
        // A frame the decoder cannot read is not worth stopping the view for; the Errors panel shows it.
      }
    })
    const timer = window.setInterval(() => {
      const time = Date.now()
      let expired = false
      for (const [id, at] of failedAt.current) {
        if (time - at >= FAILED_MS) {
          failedAt.current.delete(id)
          expired = true
        }
      }
      for (const [wire, at] of trueAt.current) {
        if (time - at >= hold.current) {
          trueAt.current.delete(wire)
          expired = true
        }
      }
      if (!dirty.current && !expired) return
      dirty.current = false
      setLive(store.current)
      setNow(time)
    }, FLUSH_MS)
    return () => {
      unsubscribe()
      window.clearInterval(timer)
    }
  }, [running])

  // The subscription: what the debug build watches plus what the user chose, resent to the board on every change (no upload).
  const room = subscribeRoom(catalog, DEFAULT_MAX_FRAME_BYTES)
  const merged = useMemo(() => (running ? withWatched(running.plan.program, running.plan.subscribed, extra, room) : undefined), [running, extra, room])
  useEffect(() => {
    if (!running || !merged) return
    setNotice(merged.dropped.length ? `${merged.dropped.length} chosen variable(s) do not fit the board's ${room} subscriptions.` : undefined)
    const key = merged.wires.join(',')
    if (key === sentKey.current) return
    sentKey.current = key
    void sendSteps(running.session, [{ label: `vm subscribe (${merged.wires.length})`, frame: packSubscribe(catalog, merged.wires, DEFAULT_MAX_FRAME_BYTES) }]).catch((failure) => setError(describeCommandError(failure)))
  }, [running, merged, room])

  const start = useCallback(async () => {
    if (!session) {
      setError('Connect a runIT board first.')
      return
    }
    const plan = planVmUpload(project, catalog, { maxFrameBytes: DEFAULT_MAX_FRAME_BYTES, sections, debug: true })
    if (!plan.ok || !plan.steps.length) {
      const problem = plan.diagnostics.find((entry) => entry.severity === 'error')
      setError(problem ? problem.message : 'The program is empty: add blocks first.')
      return
    }
    setBusy(true)
    setError(undefined)
    try {
      await sendSteps(session, plan.steps)
      const run = catalog.execCommands.get('NORMAL_MODE')
      if (run === undefined) throw new Error('The firmware has no VM_EXEC_NORMAL_MODE.')
      await sendSteps(session, [{ label: 'vm exec normal_mode', frame: packExec(catalog, run) }])
      store.current = new Map()
      failedAt.current = new Map()
      trueAt.current = new Map()
      setLive(new Map())
      sentKey.current = plan.subscribed.join(',')
      setMode('running')
      setNext(undefined)
      setStarted({ session, plan, source })
    } catch (failure) {
      setError(describeCommandError(failure))
    } finally {
      setBusy(false)
    }
  }, [project, sections, session, source])

  const run = useCallback((action: RunAction) => {
    if (!running || commanding.current) return
    const count = running.plan.program.blocks.blocks.length
    const steps: Record<RunAction, readonly string[]> = {
      run: ['NORMAL_MODE'],
      pause: ['PAUSE'],
      resume: ['RESUME'],
      step: mode === 'scan' ? ['ONCE'] : ['SCAN_MODE', 'ONCE'],
      block: ['BLOCK_MODE'],
      next: ['NEXT'],
    }
    const frames = steps[action].map((member) => {
      const command = catalog.execCommands.get(member)
      if (command === undefined) throw new Error(`The firmware has no VM_EXEC_${member}.`)
      return { label: `vm exec ${member.toLowerCase()}`, frame: packExec(catalog, command) }
    })
    commanding.current = true
    setError(undefined)
    void sendSteps(running.session, frames)
      .then(() => {
        if (action === 'pause' && mode !== 'paused') resumeMode.current = mode
        setMode(action === 'run' ? 'running' : action === 'pause' ? 'paused' : action === 'resume' ? resumeMode.current : action === 'step' ? 'scan' : 'block')
        if (action === 'block') setNext(nextBlockIndex(undefined, count, 'enter'))
        else if (action === 'next') setNext((current) => nextBlockIndex(current, count, 'next'))
        else if (action !== 'resume') setNext(undefined)
      })
      .catch((failure) => setError(describeCommandError(failure)))
      .finally(() => { commanding.current = false })
  }, [running, mode])

  const baseWires = useMemo(() => new Set(running?.plan.subscribed ?? []), [running])
  const watchState = (objectId: string): WatchState | undefined => {
    const wire = running?.plan.program.objects.wireIdOf.get(objectId)
    return wire === undefined ? undefined : baseWires.has(wire) ? 'auto' : extra.has(objectId) ? 'user' : 'off'
  }
  const watchIds = (ids: readonly string[]) => setExtra((current) => new Set([...current, ...ids]))

  const stale = !!running && running.source !== source
  // Stale = the canvas moved on since the upload: the button uploads the new build instead of stopping.
  const toggle = useCallback(() => {
    if (busy) return
    if (running && !stale) stop()
    else void start()
  }, [busy, running, stale, start, stop])
  const blocks = useMemo(() => {
    const map = new Map<string, BlockLive>()
    if (!running?.plan.debug) return map
    const recent = (wire: number) => now - (trueAt.current.get(wire) ?? -Infinity) < holdMs
    for (const watch of running.plan.debug.blocks.values()) map.set(watch.id, blockLive(watch, running.plan.program.objects, live, recent))
    return map
  }, [running, live, now, holdMs])

  return {
    active: !!running,
    busy,
    ...(error ? { error } : {}),
    stale,
    toggle,
    clearError: () => setError(undefined),
    ...(running ? { mode } : {}),
    ...(running && mode === 'block' && next !== undefined && running.plan.program.blocks.blocks[next] ? { nextBlock: running.plan.program.blocks.blocks[next]!.id } : {}),
    run,
    watchState,
    watch: (objectId) => watchIds([objectId]),
    watchPath: (path: ObjectPath) => {
      const layout = running?.plan.program.objects
      if (layout) watchIds(pathWires(layout, path).map((wire) => layout.objects[wire]!.node.id))
    },
    unwatch: (objectId) => setExtra((current) => new Set([...current].filter((id) => id !== objectId))),
    subscribed: merged?.wires.length ?? 0,
    subscribeLimit: room,
    ...(notice ? { notice } : {}),
    block: (blockId) => (stale ? undefined : blocks.get(blockId)),
    failed: (blockId) => !!running && now - (failedAt.current.get(blockId) ?? -Infinity) < FAILED_MS,
    read: (objectId) => (running && !stale ? readPath(running.plan.program.objects, live, { root: objectId }) : undefined),
  }
}
