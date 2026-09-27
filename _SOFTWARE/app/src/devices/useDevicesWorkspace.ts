import { useEffect, useMemo, useRef, useState } from 'react'
import { runitDeviceCatalog } from '../domain/descriptors'
import { checkDevices, checkPwm, checkSetup, defaultInstall, nextActionId, nextDeviceId, resolveDevice, withDeviceAliases } from '../domain/devices'
import { parseActions, parseDeviceAliases, parseDevices, parseSetup } from '../domain/project'
import type { ActionStep, DeviceRef, ProjectAction, ProjectDevice, StepValues } from '../domain/project'

/*
 * The Board view's state: the user's devices, every device's default settings
 * (`setup`) and the actions (all saved in the project file and auto-saved to
 * browser storage), what is selected, and the action composer. Edits go through `edit` with undo / redo, like the object
 * tree.
 */

const STORAGE_KEY = 'runit.devices'
const baseCatalog = runitDeviceCatalog()

export interface DevicesState {
  readonly deviceAliases?: Readonly<Record<DeviceRef, string>>
  readonly devices: readonly ProjectDevice[]
  readonly actions: readonly ProjectAction[]
  readonly setup: readonly ActionStep[]
}

export type DeviceSelection =
  | { readonly kind: 'device'; readonly ref: DeviceRef }
  | { readonly kind: 'add' }
  | { readonly kind: 'action'; readonly id: string }

interface History {
  readonly past: readonly DevicesState[]
  readonly present: DevicesState
  readonly future: readonly DevicesState[]
}

const EMPTY: DevicesState = { devices: [], actions: [], setup: [] }

/** Older app files stored a second name for user devices; keep just their editable name. */
const restoreDeviceNames = (state: DevicesState): DevicesState => {
  const aliases = parseDeviceAliases(state.deviceAliases, 'deviceAliases')
  const userIds = new Set(state.devices.map((device) => device.id))
  return {
    ...state,
    devices: state.devices.map((device) => aliases[device.id] ? { ...device, name: aliases[device.id]! } : device),
    deviceAliases: Object.fromEntries(Object.entries(aliases).filter(([ref]) => !userIds.has(ref))),
  }
}

const loadState = (): DevicesState => {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) {
      const saved = JSON.parse(raw) as { devices: unknown; actions: unknown; setup: unknown; deviceAliases?: unknown }
      return restoreDeviceNames({ devices: parseDevices(saved.devices, 'devices'), actions: parseActions(saved.actions, 'actions'), setup: parseSetup(saved.setup, 'setup'), deviceAliases: parseDeviceAliases(saved.deviceAliases, 'deviceAliases') })
    }
  } catch {
    /* Storage unavailable or unreadable: start empty. */
  }
  return EMPTY
}

const newId = (prefix: string): string => `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

const uniqueName = (taken: readonly string[], base: string): string => {
  const names = new Set(taken)
  if (!names.has(base)) return base
  for (let index = 2; ; index++) if (!names.has(`${base} ${index}`)) return `${base} ${index}`
}

export function useDevicesWorkspace(onSelect?: () => void) {
  const [history, setHistory] = useState<History>(() => ({ past: [], present: loadState(), future: [] }))
  const historyRef = useRef(history)
  const commit = (next: History) => {
    historyRef.current = next
    setHistory(next)
  }
  const [selection, setSelection] = useState<DeviceSelection>()
  /** Project ID of the action open in the composer. */
  const [composing, setComposing] = useState<string>()
  const [error, setError] = useState('')
  const { devices, actions, setup, deviceAliases } = history.present
  const catalog = useMemo(() => withDeviceAliases(baseCatalog, deviceAliases), [deviceAliases])

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(history.present))
    } catch {
      /* The project file keeps them. */
    }
  }, [history.present])

  const diagnostics = useMemo(() => [...checkDevices(catalog, devices), ...checkSetup(catalog, devices, setup), ...checkPwm(catalog, devices, setup, actions)], [catalog, devices, setup, actions])

  /** Apply a change with undo; a thrown error is shown and nothing changes. */
  const edit = (change: (current: DevicesState) => DevicesState): boolean => {
    const curr = historyRef.current
    try {
      const next = change(curr.present)
      if (next !== curr.present) commit({ past: [...curr.past, curr.present].slice(-50), present: next, future: [] })
      setError('')
      return true
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
      return false
    }
  }

  const select = (next: DeviceSelection | undefined) => {
    setSelection(next)
    if (next) onSelect?.()
  }

  const addDevice = (typeId: string) => {
    const type = catalog.type(typeId)
    if (!type) return
    const deviceId = nextDeviceId(catalog, historyRef.current.present.devices)
    if (deviceId === undefined) {
      setError(`No free device ID left (up to ${catalog.maxDeviceId}).`)
      return
    }
    const device: ProjectDevice = {
      id: newId('device'),
      deviceId,
      type: type.id,
      name: uniqueName(historyRef.current.present.devices.map((entry) => entry.name), type.title.split(' ')[0] ?? type.title),
      tags: [],
      install: defaultInstall(type, catalog.i2cBuses.user),
    }
    if (edit((current) => ({ ...current, devices: [...current.devices, device] }))) select({ kind: 'device', ref: device.id })
  }

  const updateDevice = (id: string, patch: Partial<Omit<ProjectDevice, 'id' | 'type'>>) =>
    edit((current) => ({ ...current, devices: current.devices.map((device) => (device.id === id ? { ...device, ...patch } : device)) }))

  const removeDevice = (id: string) => {
    const removed = edit((current) => ({ ...current, devices: current.devices.filter((device) => device.id !== id), setup: current.setup.filter((step) => step.device !== id), deviceAliases: Object.fromEntries(Object.entries(current.deviceAliases ?? {}).filter(([ref]) => ref !== id)) }))
    if (removed && selection?.kind === 'device' && selection.ref === id) setSelection(undefined)
  }

  const setDeviceAlias = (ref: DeviceRef, value: string) => edit((current) => {
    if (!resolveDevice(baseCatalog, current.devices, ref)) return current
    const alias = value.trim()
    if ((current.deviceAliases?.[ref] ?? '') === alias) return current
    const aliases = { ...current.deviceAliases }
    if (alias) aliases[ref] = alias
    else delete aliases[ref]
    return { ...current, deviceAliases: aliases }
  })

  // Default settings -------------------------------------------------------

  const SET_MODE = 'packet_sys_io_set_mode_t'
  const SET_LEVEL = 'packet_sys_io_set_level_t'
  const pinStep = (steps: readonly ActionStep[], ref: DeviceRef, contract: string, pin: number) => steps.find((step) => step.device === ref && step.contract === contract && step.values.pin === pin)

  /** The pin's default mode; undefined clears it (and its level). */
  const setPinMode = (ref: DeviceRef, pin: number, mode: number | undefined) => edit((current) => {
    const existing = pinStep(current.setup, ref, SET_MODE, pin)
    if (mode === undefined) return { ...current, setup: current.setup.filter((step) => !(step.device === ref && step.values.pin === pin && (step.contract === SET_MODE || step.contract === SET_LEVEL))) }
    if (existing) return { ...current, setup: current.setup.map((step) => (step === existing ? { ...step, values: { ...step.values, mode } } : step)) }
    return { ...current, setup: [...current.setup, { id: newId('setup'), device: ref, contract: SET_MODE, values: { pin, mode } }] }
  })

  /** The pin's level after boot; undefined leaves it as the mode sets it. */
  const setPinLevel = (ref: DeviceRef, pin: number, level: boolean | undefined) => edit((current) => {
    const existing = pinStep(current.setup, ref, SET_LEVEL, pin)
    if (level === undefined) return { ...current, setup: current.setup.filter((step) => step !== existing) }
    if (existing) return { ...current, setup: current.setup.map((step) => (step === existing ? { ...step, values: { ...step.values, level: level ? 1 : 0 } } : step)) }
    return { ...current, setup: [...current.setup, { id: newId('setup'), device: ref, contract: SET_LEVEL, values: { pin, level: level ? 1 : 0 } }] }
  })

  const addSetup = (ref: DeviceRef, contract: string, values: StepValues) =>
    edit((current) => ({ ...current, setup: [...current.setup, { id: newId('setup'), device: ref, contract, values }] }))

  const updateSetup = (id: string, patch: Partial<Omit<ActionStep, 'id' | 'device'>>) =>
    edit((current) => ({ ...current, setup: current.setup.map((step) => (step.id === id ? { ...step, ...patch } : step)) }))

  const removeSetup = (id: string) => edit((current) => ({ ...current, setup: current.setup.filter((step) => step.id !== id) }))

  /** A new action, optionally with a first step; opens the composer on it. */
  const addAction = (firstStep?: Omit<ActionStep, 'id'>): string | undefined => {
    const actionId = nextActionId(historyRef.current.present.actions)
    if (actionId === undefined) {
      setError('All 255 action IDs are used.')
      return undefined
    }
    const action: ProjectAction = {
      id: newId('action'),
      actionId,
      name: uniqueName(historyRef.current.present.actions.map((entry) => entry.name), 'action'),
      steps: firstStep ? [{ ...firstStep, id: newId('step') }] : [],
    }
    if (!edit((current) => ({ ...current, actions: [...current.actions, action] }))) return undefined
    setComposing(action.id)
    return action.id
  }

  const updateAction = (id: string, patch: Partial<Omit<ProjectAction, 'id'>>) =>
    edit((current) => {
      if (patch.actionId !== undefined && current.actions.some((action) => action.id !== id && action.actionId === patch.actionId)) throw new Error(`Action ID ${patch.actionId} is taken.`)
      return { ...current, actions: current.actions.map((action) => (action.id === id ? { ...action, ...patch } : action)) }
    })

  const addStep = (actionId: string, step: Omit<ActionStep, 'id'>) => {
    const action = historyRef.current.present.actions.find((entry) => entry.id === actionId)
    if (action) updateAction(actionId, { steps: [...action.steps, { ...step, id: newId('step') }] })
  }

  const removeAction = (id: string) => {
    if (!edit((current) => ({ ...current, actions: current.actions.filter((action) => action.id !== id) }))) return
    if (composing === id) setComposing(undefined)
    if (selection?.kind === 'action' && selection.id === id) setSelection(undefined)
  }

  /** Open the composer: on an action, or a new one; `step` is appended to it. */
  const compose = (actionId?: string, step?: Omit<ActionStep, 'id'>) => {
    if (actionId) {
      setComposing(actionId)
      if (step) addStep(actionId, step)
    } else addAction(step)
  }

  /** Replace devices and actions (project opened or recovered); undo goes back. */
  const load = (next: DevicesState) => {
    const curr = historyRef.current
    commit({ past: [...curr.past, curr.present], present: restoreDeviceNames({ devices: [...next.devices], actions: [...next.actions], setup: [...next.setup], deviceAliases: next.deviceAliases }), future: [] })
    setSelection(undefined)
    setComposing(undefined)
    setError('')
  }

  const undo = () => {
    const curr = historyRef.current
    if (!curr.past.length) return
    commit({ past: curr.past.slice(0, -1), present: curr.past.at(-1)!, future: [curr.present, ...curr.future] })
  }
  const redo = () => {
    const curr = historyRef.current
    if (!curr.future.length) return
    commit({ past: [...curr.past, curr.present], present: curr.future[0]!, future: curr.future.slice(1) })
  }

  return {
    catalog,
    deviceAliases,
    setDeviceAlias,
    devices,
    actions,
    setup,
    diagnostics,
    error,
    selection,
    select,
    composing: composing ? actions.find((action) => action.id === composing) : undefined,
    compose,
    closeComposer: () => setComposing(undefined),
    addDevice,
    updateDevice,
    removeDevice,
    setPinMode,
    setPinLevel,
    addSetup,
    updateSetup,
    removeSetup,
    addAction,
    updateAction,
    addStep,
    removeAction,
    load,
    canUndo: history.past.length > 0,
    canRedo: history.future.length > 0,
    undo,
    redo,
  }
}

export type DevicesWorkspace = ReturnType<typeof useDevicesWorkspace>
