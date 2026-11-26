import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { createEngine } from '@engine'
import { bundledServiceCatalog, combineResolvers, CommandError, decodeEvent, deviceRefs, ErrorInterpreter, programRefs, Services } from '@backend'
import type { CatalogEntry, FieldValues, LinkState, RunitLink } from '@backend'
import type { ProfileState, BoardProfileState } from './useBoardProfile'
import type { useProject } from '../project/useProject'
import { parseProject, serializeProject } from '../project/projectFile'
import type { ProjectAction, ProjectDevice, Step } from '../project/projectFile'
import { EMPTY_LAYOUT } from '../code/canvas/layout'
import { forgetDevice } from '../hardware/apply'
import { boardHoldsJson, readBoardProfile, writeBoardProfile } from './boardProfile'
import { bootSteps, withBoard } from './boardEditor'
import { readFromBoard, writeToBoard } from './boardSync'
import type { SyncProgress } from './boardSync'
import { baseConfig, decodeOps, DEVICE_COMMANDS, DEVICE_EVENTS, deviceList, hydrateRemoteDevices, lifecycleOf, nextActionId, nextDeviceId, pinNamer, renumberDevice, SINGLE_TYPES } from './devices'
import type { DeviceInfo, DeviceOps, RemoteBoardContext } from './devices'

type ProjectState = Pick<ReturnType<typeof useProject>, 'project' | 'edit'>

/** What is selected in the Devices view: a device (by its ID) or an action. */
/** A device, an action, or the choice of a type to add (`add`: the centre shows a tile for every type that can be installed; optional `board` places it on that remote board). */
export type DeviceSelection = { readonly kind: 'device'; readonly id: number } | { readonly kind: 'action'; readonly id: number } | { readonly kind: 'add'; readonly board?: number }

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
/**
 * `autoSync`: read the board into the project each time it connects (when the project has nothing unwritten).
 *
 * `profile` is the board's own profile (useBoardProfile.ts). In the app's Devices view (`scope: 'app'`) its devices are the board's
 * system devices. In the Project view's board editor (`scope: 'board'`) the
 * edited project IS the profile: its devices are added, changed and removed like a project's, and Write and Read are the profile's
 * to and from the board; it has no actions and no program. When the board connects, this instance loads the board's profile.
 */
export function useDevices(link: RunitLink, linkState: LinkState, project: ProjectState, autoSync = false, profile?: BoardProfileState, scope: 'app' | 'board' = 'app', remoteContext?: RemoteBoardContext, onAfterWrite?: () => Promise<void>) {
  const catalog = useMemo(bundledServiceCatalog, [])
  const board = scope === 'board' ? profile!.bare : profile!.board
  const boot = useMemo(() => bootSteps(profile!.project), [profile!.project])
  const engine = useMemo(() => createEngine(), [])
  const services = useMemo(() => new Services(link, catalog), [link, catalog])
  const lifecycle = useMemo(() => lifecycleOf(catalog), [catalog])
  const connected = linkState === 'connected'
  const devices = useMemo(() => hydrateRemoteDevices(deviceList(project.project, catalog, board), project.project, scope === 'app' ? remoteContext : undefined), [project.project, catalog, board, scope, remoteContext])
  const [selection, setSelection] = useState<DeviceSelection | undefined>()
  /** The pin whose settings the right panel shows (of the selected device). */
  const [pinSelection, setPinSelection] = useState<{ readonly device: number; readonly pin: number } | undefined>()
  const select = useCallback((next: DeviceSelection | undefined) => { setSelection(next); setPinSelection(undefined) }, [])
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
  /**
   * Which of the view's devices the board has, their types and whether they are suspended: one LIST (SYS_DEVICE.MD) to the ID that
   * addresses no device, not an INFO to each, which the board answers with an error for every one it does not have.
   */
  const probe = useCallback(async () => {
    if (!connected || services.catalog.allDevices === undefined) return
    try {
      const { value } = await services.device(services.catalog.allDevices, lifecycle.id, DEVICE_COMMANDS.list, undefined, { side: 'read', label: 'devices LIST (probe)' })
      const rows = new Map((value as readonly Record<string, unknown>[]).map((r) => [Number(r.id), r]))
      const next = new Map<number, LiveDevice>()
      for (const id of ids ? ids.split(',').map(Number) : []) {
        const row = rows.get(id)
        next.set(id, row ? { state: 'present', typeId: Number(row.type_id), suspended: row.suspended === true || Number(row.suspended) === 1 } : { state: 'missing' })
      }
      for (const [id, l] of next) if (l.state === 'present') occupied.current.add(id)
      setLive(next)
    } catch {
      // The link dropped: the next probe asks again.
    }
  }, [connected, services, lifecycle, ids])

  useEffect(() => {
    if (connected) void probe()
    else { occupied.current.clear(); setLive(new Map()) }
  }, [connected, probe])

  // The board says when a device is suspended or resumed (the DEVICE events, SYS_DEVICE.MD): the view follows it without
  // asking. A sweep (the safe state, a critical fault, every device at once) comes as one event from SYS_DEVICE_ID_NONE: then
  // every device is asked again.
  useEffect(() => link.on('events', (data) => {
    const event = decodeEvent(catalog, data)
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

  const addDevice = useCallback((type: CatalogEntry, onBoard?: number) => {
    if (SINGLE_TYPES.has(type.id)) return
    const id = nextDeviceId(project.project, board, occupied.current)
    const taken = new Set(project.project.devices.map((d) => d.name))
    let n = 1
    while (taken.has(`${type.label} ${n}`)) n++
    edit((p) => ({
      ...p,
      devices: [...p.devices, { id, type: type.id, name: `${type.label} ${n}`, config: baseConfig(type, board, scope), defaults: [], ...(onBoard !== undefined ? { board: onBoard } : {}) }],
    }))
    setSelection({ kind: 'device', id })
  }, [edit, project.project, board, scope])

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
    if (scope === 'board') {
      // The profile, then a restart: only a boot creates the devices.
      await writeBoardProfile(link, catalog, profile!.board, project.project, boot)
      await services.call('SYS', 'RESTART')
      setSynced(snapshot)
      return 'The profile is on the board; it restarts to create its devices.'
    }
    const text = await writeToBoard(link, catalog, board, project.project, setBusy)
    await onAfterWrite?.()
    setSynced(snapshot)
    return text
  }), [run, scope, link, catalog, board, project.project, snapshot, boot, profile, services, onAfterWrite])

  const read = useCallback(() => run(async () => {
    if (scope === 'board') {
      const stored = await readBoardProfile(link, catalog)
      if (!stored) return 'The board keeps no profile: nothing read.'
      const read = withBoard(stored, profile!.base)
      edit(() => read)
      setSynced(serializeProject(read))
      return 'Read the profile from the board.'
    }
    const result = await readFromBoard()
    const read = result.project
    if (read) {
      edit(() => read)
      setSynced(serializeProject(read))
    }
    return result.notes.join(' ')
  }), [run, scope, link, catalog, edit])

  // A board that connects gives the app its profile (SYS_BOARD.MD): what the board is comes from the board, so the editor and the
  // Devices view show it with no click. Not over edits of the profile that are not written yet (Read does it by hand, Undo goes
  // back). A board that keeps none gets the app's and restarts (only a boot creates its devices), once per run of the app. The
  // board editor's instance does it: its project is the profile, and what it loads is what the board holds.
  const profileLatest = useRef({ project: project.project, pending, snapshot, board: profile?.board, boot, edit, explain })
  profileLatest.current = { project: project.project, pending, snapshot, board: profile?.board, boot, edit, explain }
  const profileProvisioned = useRef(false)
  useEffect(() => {
    if (!connected || scope !== 'board') return
    void (async () => {
      try {
        // The same profile (most connections) is told by length and CRC; only another one is read whole.
        const before = profileLatest.current
        if (await boardHoldsJson(link, catalog, before.project)) return setSynced(before.snapshot)
        const stored = await readBoardProfile(link, catalog)
        const now = profileLatest.current
        if (stored) {
          const loaded = withBoard(stored, profile!.base)
          const text = serializeProject(loaded)
          if (text === serializeProject(now.project)) return setSynced(text)
          if (now.pending) return setNote({ tone: 'info', text: 'Not loaded from the board: the profile has changes that are not written yet. Read does it by hand (Undo goes back).' })
          now.edit(() => loaded)
          setSynced(text)
          setNote({ tone: 'info', text: `Loaded the board's profile: ${loaded.name}.` })
          return
        }
        if (profileProvisioned.current) return setNote({ tone: 'error', text: 'The board does not hold the profile written to it.' })
        profileProvisioned.current = true
        setNote({ tone: 'info', text: 'The board keeps no profile: writing this one, it restarts to create its devices.' })
        await writeBoardProfile(link, catalog, now.board!, now.project, now.boot)
        await services.call('SYS', 'RESTART')
        setSynced(now.snapshot)
      } catch (failure) {
        setNote({ tone: 'error', text: profileLatest.current.explain(failure) })
      }
    })()
    // Only when the link comes up: what else it uses is read from `profileLatest`.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connected])

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
    scope, project: project.project, catalog, board, services, lifecycle, connected, devices, selection, select, selectPin, clearPin, pinSelection, selectedDevice,
    live, ops, probe, refreshOps, nameOf, pinName, explain, interpreter, setPinAlias, pinKinds, setPinKind, resetPinKinds,
    updateDevice, addDevice, removeDevice, setDefaults, addAction, updateAction, removeAction,
    write: () => void write(), read: () => void read(), pending, busy, note, clearNote: () => setNote(undefined),
  }
}

export type DevicesState = ReturnType<typeof useDevices>
