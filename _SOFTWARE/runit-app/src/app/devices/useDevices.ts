import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { createEngine } from '@engine'
import { bundledBoard, bundledServiceCatalog, combineResolvers, CommandError, decodeEvent, deviceRefs, ErrorInterpreter, programRefs, Services } from '@backend'
import type { CatalogEntry, LinkState, RunitLink } from '@backend'
import type { useProject } from '../project/useProject'
import { parseProject, serializeProject } from '../project/projectFile'
import type { ProjectAction, ProjectDevice, Step } from '../project/projectFile'
import { EMPTY_LAYOUT } from '../code/canvas/layout'
import { forgetDevice } from '../hardware/apply'
import { readFromBoard, writeToBoard } from './boardSync'
import type { SyncProgress } from './boardSync'
import { baseConfig, decodeOps, DEVICE_COMMANDS, DEVICE_EVENTS, deviceList, lifecycleOf, nextActionId, nextDeviceId, pinNamer, renumberDevice, SINGLE_TYPES } from './devices'
import type { DeviceInfo, DeviceOps } from './devices'

type ProjectState = ReturnType<typeof useProject>

/** What is selected in the Devices view: a device (by its ID) or an action. */
/** A device, an action, or the choice of a type to add (`add`: the centre shows a tile for every type that can be installed). */
export type DeviceSelection = { readonly kind: 'device'; readonly id: number } | { readonly kind: 'action'; readonly id: number } | { readonly kind: 'add' }

/** A device as the board answers INFO: there and of which type, suspended or not; or not there. */
export type LiveDevice =
  | { readonly state: 'present'; readonly typeId: number; readonly suspended: boolean }
  | { readonly state: 'missing' }

/** A line for the user about the last thing sent to the board. */
export interface DevicesNote {
  readonly tone: 'info' | 'error'
  readonly text: string
}

/**
 * The Devices view's state: the devices (the board's and the project's), what is selected, what the board answers about each
 * (INFO, and OPS for the selected one), the project's edits of devices and actions, writing to and reading from the board, and
 * a failure read for a user (the error record with the devices' names).
 */
/** `autoSync`: read the board into the project each time it connects (when the project has nothing unwritten). */
export function useDevices(link: RunitLink, linkState: LinkState, project: ProjectState, autoSync = false) {
  const catalog = useMemo(bundledServiceCatalog, [])
  const board = useMemo(bundledBoard, [])
  const engine = useMemo(() => createEngine(), [])
  const services = useMemo(() => new Services(link, catalog), [link, catalog])
  const lifecycle = useMemo(() => lifecycleOf(catalog), [catalog])
  const connected = linkState === 'connected'
  const devices = useMemo(() => deviceList(project.project, catalog, board), [project.project, catalog, board])
  const [selection, setSelection] = useState<DeviceSelection | undefined>()
  /** The pin whose settings the right panel shows (of the selected device). */
  const [pinSelection, setPinSelection] = useState<{ readonly device: number; readonly pin: number } | undefined>()
  const select = useCallback((next: DeviceSelection) => { setSelection(next); setPinSelection(undefined) }, [])
  const selectPin = useCallback((device: number, pin: number) => { setSelection({ kind: 'device', id: device }); setPinSelection({ device, pin }) }, [])
  const clearPin = useCallback(() => setPinSelection(undefined), [])
  const [live, setLive] = useState<ReadonlyMap<number, LiveDevice>>(new Map())
  // Every ID the board has answered for since the link opened: a device there keeps its ID, even when no project device uses it now.
  const occupied = useRef(new Set<number>())
  const [ops, setOps] = useState<DeviceOps | undefined>()
  const [busy, setBusy] = useState<SyncProgress | undefined>()
  const [note, setNote] = useState<DevicesNote | undefined>()
  // The state this session put pins in with live commands (`device/pin` -> a kind, or null after a reset): the board cannot be asked,
  // so a command's pin list follows it (pinStateOf). A board restart or a lost link forgets it: the boot and the start commands set the pins again.
  const [pinKinds, setPinKinds] = useState<ReadonlyMap<string, string | null>>(new Map())
  const setPinKind = useCallback((device: number, pin: number, kind: string | null) => setPinKinds((m) => new Map(m).set(`${device}/${pin}`, kind)), [])
  const resetPinKinds = useCallback((device: number) => setPinKinds((m) => new Map([...m].filter(([key]) => !key.startsWith(`${device}/`)))), [])

  const names = useMemo(() => new Map(devices.map((d) => [d.id, d.name])), [devices])
  const nameOf = useCallback((id: number) => names.get(id), [names])
  const pinName = useMemo(() => pinNamer(devices), [devices])

  // What the board holds, as the project was at the last write or read: a difference is something to write (the toolbar's Write).
  // The canvas and the power display are the app's own: moving a block or changing what the display reads is nothing to write.
  const snapshot = useMemo(() => serializeProject({ ...project.project, canvas: EMPTY_LAYOUT, powerDisplay: undefined, hardware: undefined }), [project.project])
  const [synced, setSynced] = useState<string | undefined>()
  useEffect(() => { setSynced((s) => s ?? snapshot) }, [snapshot])
  const pending = synced !== undefined && synced !== snapshot
  // The program on the board is the one of that project, compiled: its ids lead an error's objects, accessors and blocks back to
  // the user's variables and blocks (`programRefs`). A program that does not compile was never written.
  const boardRefs = useMemo(() => {
    const parsed = synced === undefined ? undefined : parseProject(synced)
    if (!parsed?.ok) return undefined
    const result = engine.compile(parsed.project.program)
    return result.ok ? programRefs(parsed.project.program, result) : undefined
  }, [synced, engine])
  // The board's errors name pins as the user does: an alias first, then the devices' names, then the program's variables and blocks.
  const interpreter = useMemo(() => new ErrorInterpreter({
    services: catalog,
    resolve: combineResolvers((ref) => {
      const alias = (ref.kind === 'pin' || ref.kind === 'channel') && ref.scope?.kind === 'device' ? devices.find((d) => d.id === ref.scope!.id)?.pinAliases.get(ref.id) : undefined
      return alias === undefined ? undefined : { label: `#${ref.id} ${alias}`, target: { kind: ref.kind, device: ref.scope!.id, [ref.kind]: ref.id } }
    }, deviceRefs(names), boardRefs),
  }), [catalog, names, devices, boardRefs])

  /** A failure as a user reads it: the root cause of the board's error record, and where. */
  const explain = useCallback((failure: unknown): string => {
    if (failure instanceof CommandError && failure.record) {
      const report = interpreter.interpret(failure.record)
      const where = report.where[report.where.length - 1]?.replace(/^In /, '')
      return where && !report.message.includes(where) ? `${report.message} (${report.where[report.where.length - 1]})` : report.message
    }
    return failure instanceof Error ? failure.message : String(failure)
  }, [interpreter])

  // The IDs as one string: a rename does not ask the board again.
  const ids = devices.map((d) => d.id).join(',')
  /** Asks every device of the view INFO: there or not, its type, suspended. */
  const probe = useCallback(async () => {
    if (!connected) return
    const next = new Map<number, LiveDevice>()
    await Promise.all((ids ? ids.split(',').map(Number) : []).map(async (id) => {
      try {
        const { value } = await services.device(id, lifecycle.id, DEVICE_COMMANDS.info, undefined, { label: `device ${id} INFO (probe)` })
        const info = value as Record<string, unknown>
        next.set(id, { state: 'present', typeId: Number(info.type_id), suspended: Number(info.suspended) === 1 })
      } catch (failure) {
        if (failure instanceof CommandError && failure.code === 'device-error') next.set(id, { state: 'missing' })      }
    }))
    for (const [id, l] of next) if (l.state === 'present') occupied.current.add(id)
    setLive(next)
  }, [connected, services, lifecycle, ids])

  useEffect(() => {
    if (connected) void probe()
    else { occupied.current.clear(); setLive(new Map()) }
  }, [connected, probe])


  // The board says when a device is suspended or resumed (the DEVICE events, SYS_DEVICE.MD): the view follows it without
  // asking. A sweep (the safe state, a critical fault, every device at once) comes as one event from SYS_DEVICE_ID_NONE: then
  // every device is asked again.
  useEffect(() => link.on('events', (data) => {
    const event = decodeEvent(link.defs, catalog, data)
    if (!event?.event || event.contract !== lifecycle) return
    const suspended = event.event.id === DEVICE_EVENTS.suspended ? true : event.event.id === DEVICE_EVENTS.resumed ? false : undefined
    if (suspended === undefined) return
    const what = suspended ? 'suspended' : 'resumed'
    if (event.source === catalog.allDevices) {
      void probe()
      setNote({ tone: 'info', text: `The board ${what} its devices${suspended ? ' (safe state or a fault)' : ''}.` })
      return
    }
    setLive((live) => {
      const known = live.get(event.source)
      if (known?.state !== 'present') return live
      return new Map(live).set(event.source, { ...known, suspended })
    })
    setNote({ tone: 'info', text: `${names.get(event.source) ?? `Device ${event.source}`} ${what}.` })
  }), [link, catalog, lifecycle, probe, names])

  const selectedDevice = selection?.kind === 'device' ? devices.find((d) => d.id === selection.id) : undefined
  /** OPS of the selected device: which commands it answers, and their sides. */
  const selectedId = selectedDevice?.id
  const refreshOps = useCallback(async () => {
    setOps(undefined)
    // The board's OPS answer is the device's own only when the board has this type under the ID (else it is another device's).
    const known = selectedId === undefined ? undefined : live.get(selectedId)
    if (!connected || selectedId === undefined || known?.state !== 'present' || (selectedDevice?.type && known.typeId !== selectedDevice.type.typeId)) return
    try {
      const { value } = await services.device(selectedId, lifecycle.id, DEVICE_COMMANDS.ops)
      setOps(decodeOps(value))
    } catch {
      setOps(undefined)
    }
  }, [connected, selectedId, selectedDevice, live, services, lifecycle])
  useEffect(() => void refreshOps(), [refreshOps])

  /* ---- edits of the project ---- */

  const { edit } = project

  // A project device whose ID the board gives to another type (a device left there by an earlier project) moves to a free ID:
  // the board keeps the other device, and the project's references follow.
  useEffect(() => {
    const clash = devices.find((d) => {
      const known = live.get(d.id)
      return !d.system && d.type !== undefined && known?.state === 'present' && known.typeId !== d.type.typeId
    })
    if (!clash) return
    occupied.current.add(clash.id)
    const to = nextDeviceId(project.project, board, occupied.current)
    edit((p) => renumberDevice(p, clash.id, to))
    setSelection((s) => (s?.kind === 'device' && s.id === clash.id ? { kind: 'device', id: to } : s))
    setNote({ tone: 'info', text: `${clash.name} is ID ${to} now: ID ${clash.id} is another device on the board.` })
  }, [live, devices, board, project.project, edit])
  const updateDevice = useCallback((id: number, change: (d: ProjectDevice) => ProjectDevice, key?: string) =>
    edit((p) => ({ ...p, devices: p.devices.map((d) => (d.id === id ? change(d) : d)) }), key), [edit])

  const addDevice = useCallback((type: CatalogEntry) => {
    if (SINGLE_TYPES.has(type.id)) return
    const id = nextDeviceId(project.project, board, occupied.current)
    const taken = new Set(project.project.devices.map((d) => d.name))
    let n = 1
    while (taken.has(`${type.label} ${n}`)) n++
    edit((p) => ({ ...p, devices: [...p.devices, { id, type: type.id, name: `${type.label} ${n}`, config: baseConfig(type, board), defaults: [] }] }))
    setSelection({ kind: 'device', id })
  }, [edit, project.project, board])

  const removeDevice = useCallback((id: number) => {
    // Its part leaves the Devices canvas too, with its wires.
    edit((p) => ({ ...p, devices: p.devices.filter((d) => d.id !== id), hardware: forgetDevice(p.hardware, id) }))
    setSelection((s) => (s?.kind === 'device' && s.id === id ? undefined : s))
  }, [edit])

  /** The start commands of a device: a project device keeps them itself, a board device in `boardDefaults`. */
  const setDefaults = useCallback((device: DeviceInfo, change: (steps: readonly Step[]) => readonly Step[]) => edit((p) => {
    if (!device.system) return { ...p, devices: p.devices.map((d) => (d.id === device.id ? { ...d, defaults: change(d.defaults ?? []) } : d)) }
    const steps = change(p.boardDefaults.find((b) => b.device === device.id)?.steps ?? [])
    const others = p.boardDefaults.filter((b) => b.device !== device.id)
    return { ...p, boardDefaults: steps.length > 0 ? [...others, { device: device.id, steps }].sort((a, b) => a.device - b.device) : others }
  }), [edit])

  const addAction = useCallback((steps: readonly Step[] = []): number => {
    const id = nextActionId(project.project)
    edit((p) => ({ ...p, actions: [...p.actions, { id, name: `Action ${id}`, steps }] }))
    return id
  }, [edit, project.project])

  const updateAction = useCallback((id: number, change: (a: ProjectAction) => ProjectAction, key?: string) =>
    edit((p) => ({ ...p, actions: p.actions.map((a) => (a.id === id ? change(a) : a)) }), key), [edit])

  /** The user's name of a pin; an empty name removes it. */
  const setPinAlias = useCallback((device: number, pin: number, name: string) => edit((p) => {
    const others = p.pinAliases.filter((a) => !(a.device === device && a.pin === pin))
    return { ...p, pinAliases: name.trim() ? [...others, { device, pin, name: name.trim() }] : others }
  }, `pin-alias:${device}:${pin}`), [edit])

  const removeAction = useCallback((id: number) => {
    edit((p) => ({ ...p, actions: p.actions.filter((a) => a.id !== id) }))
    setSelection((s) => (s?.kind === 'action' && s.id === id ? undefined : s))
  }, [edit])

  /* ---- the board ---- */

  const run = useCallback(async (work: () => Promise<string>) => {
    setBusy({ step: 'Starting', done: 0, total: 1 })
    setNote(undefined)
    try {
      setNote({ tone: 'info', text: await work() })
    } catch (failure) {
      setNote({ tone: 'error', text: explain(failure) })
    } finally {
      setBusy(undefined)
    }
  }, [explain])

  const write = useCallback(() => run(async () => {
    const text = await writeToBoard(link, catalog, board, engine, project.project, setBusy)
    setSynced(snapshot)
    return text
  }), [run, link, catalog, board, engine, project.project, snapshot])

  const read = useCallback(() => run(async () => {
    const result = await readFromBoard(link, catalog, board, project.project, setBusy)
    edit(() => result.project)
    setSynced(serializeProject(result.project))
    return result.notes.join(' ')
  }), [run, link, catalog, board, project.project, edit])

  // A board that has just connected (again, after a restart or a drop) is read into the project, so what the app shows is what the
  // board has. Not over changes that are not written yet: those are the user's, Read does it by hand (Undo goes back).
  const latest = useRef({ read, pending, autoSync })
  latest.current = { read, pending, autoSync }
  const wasConnected = useRef(false)
  useEffect(() => {
    const up = connected && !wasConnected.current
    wasConnected.current = connected
    if (!connected || up) setPinKinds(new Map())
    // What failed while the board was away ("not connected") is no longer true.
    if (up) setNote((n) => (n?.tone === 'error' ? undefined : n))
    if (!up || !latest.current.autoSync) return
    // A moment for the link to settle after it comes up.
    const timer = window.setTimeout(() => {
      if (latest.current.pending) setNote({ tone: 'info', text: 'Not read from the board: the project has changes that are not written yet. Read does it by hand (Undo goes back).' })
      else void latest.current.read()
    }, 600)
    return () => window.clearTimeout(timer)
  }, [connected])

  return {
    project: project.project, catalog, board, services, lifecycle, connected, devices, selection, select, selectPin, clearPin, pinSelection, selectedDevice,
    live, ops, probe, refreshOps, nameOf, pinName, explain, interpreter, setPinAlias, pinKinds, setPinKind, resetPinKinds,
    updateDevice, addDevice, removeDevice, setDefaults, addAction, updateAction, removeAction,
    write: () => void write(), read: () => void read(), pending, busy, note, clearNote: () => setNote(undefined),
  }
}

export type DevicesState = ReturnType<typeof useDevices>
