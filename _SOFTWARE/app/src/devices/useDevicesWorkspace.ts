import { useCallback, useMemo, useState } from 'react'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { actionProblems, actionsFromJson, stepsFromJson, BOARD_DEVICES, decodeRecords, deviceProblems, devicesFromJson, foreignActions, newAction, newDevice, recordImages, userActionId } from '../domain/devices'
import type { ActionStep, ProjectAction, ProjectDevice, StoredDeviceState } from '../domain/devices'


/*
 * The Devices tab: the board's devices (from the firmware, fixed) and the user's own (auto-saved to browser storage and kept in the
 * project file), what is selected, and the names the project gives the board's devices.
 */

const STORAGE_KEY = 'runit.devices'

/** What the middle shows: every device, the device types to add, or one device (`board:<id>` for a board device). */
export type DeviceSelection = { readonly kind: 'overview' } | { readonly kind: 'add' } | { readonly kind: 'device'; readonly ref: string } | { readonly kind: 'action'; readonly id: string }

export interface DevicesState {
  readonly devices: readonly ProjectDevice[]
  /** The project's own names for board devices, by device ID. */
  readonly boardNames: Readonly<Record<string, string>>
  /** Custom aliases for pins, keyed by device ref (`board:<id>` or user device ID) and pin number. */
  readonly pinAliases?: Readonly<Record<string, Readonly<Record<string, string>>>>
  /** The default settings of every device (the board's and the user's): commands the board runs after installing it. */
  readonly setup: readonly ActionStep[]
  readonly actions: readonly ProjectAction[]
  /** The action IDs this project put on the board (or read from it): a save may remove only those. */
  readonly onBoard: readonly number[]
}

const fromJson = (raw: string): DevicesState | undefined => {
  const value: unknown = JSON.parse(raw)
  if (typeof value !== 'object' || value === null) return undefined
  const record = value as Record<string, unknown>
  const names = typeof record.boardNames === 'object' && record.boardNames !== null ? Object.fromEntries(Object.entries(record.boardNames).filter((entry): entry is [string, string] => typeof entry[1] === 'string')) : {}
  const rawPinAliases = typeof record.pinAliases === 'object' && record.pinAliases !== null ? (record.pinAliases as Record<string, unknown>) : undefined
  const pinAliases = rawPinAliases
    ? Object.fromEntries(
        Object.entries(rawPinAliases).map(([devRef, pMap]) => [
          devRef,
          typeof pMap === 'object' && pMap !== null
            ? Object.fromEntries(Object.entries(pMap).filter((entry): entry is [string, string] => typeof entry[1] === 'string'))
            : {},
        ]),
      )
    : undefined
  const onBoard = Array.isArray(record.onBoard) ? record.onBoard.filter((id): id is number => typeof id === 'number') : []
  return {
    devices: devicesFromJson(record.devices),
    boardNames: names,
    ...(pinAliases ? { pinAliases } : {}),
    setup: stepsFromJson(record.setup),
    actions: actionsFromJson(record.actions),
    onBoard,
  }
}

export const boardRef = (id: number): string => `board:${id}`

export function useDevicesWorkspace() {
  const [state, setState] = useState<DevicesState>(() => readStored(STORAGE_KEY, fromJson, { devices: [], boardNames: {}, pinAliases: {}, setup: [], actions: [], onBoard: [] }))
  const [selection, setSelection] = useState<DeviceSelection>({ kind: 'overview' })
  usePersistEffect(STORAGE_KEY, state)

  const select = useCallback((next: DeviceSelection) => setSelection(next), [])

  const addDevice = useCallback((typeId: string) => {
    // Made here, not in the state updater: StrictMode runs an updater twice, and the selection must name the device that is kept.
    const device = newDevice(typeId, state.devices)
    setState((current) => ({ ...current, devices: [...current.devices, device] }))
    setSelection({ kind: 'device', ref: device.id })
  }, [state.devices])

  const updateDevice = useCallback((id: string, patch: Partial<Omit<ProjectDevice, 'id' | 'type'>>) =>
    setState((current) => ({ ...current, devices: current.devices.map((device) => (device.id === id ? { ...device, ...patch } : device)) })), [])

  const removeDevice = useCallback((id: string) => {
    setState((current) => {
      const gone = current.devices.find((device) => device.id === id)
      const { [id]: _goneAliases, ...restAliases } = current.pinAliases ?? {}
      return {
        ...current,
        devices: current.devices.filter((device) => device.id !== id),
        pinAliases: restAliases,
        setup: gone ? current.setup.filter((step) => step.device !== gone.deviceId) : current.setup,
      }
    })
    setSelection((current) => (current.kind === 'device' && current.ref === id ? { kind: 'overview' } : current))
  }, [])

  /** A name for a board device in this project; empty goes back to the firmware's. */
  const renameBoardDevice = useCallback((id: number, name: string) =>
    setState((current) => {
      const { [String(id)]: _old, ...rest } = current.boardNames
      return { ...current, boardNames: name.trim() ? { ...rest, [String(id)]: name.trim() } : rest }
    }), [])

  const boardName = useCallback((id: number) => state.boardNames[String(id)] ?? BOARD_DEVICES.find((device) => device.id === id)?.alias ?? `Device ${id}`, [state.boardNames])

  /** Replace everything (a project opened). */
  const load = useCallback((next: Omit<DevicesState, 'onBoard'> & Partial<Pick<DevicesState, 'onBoard'>>) => {
    setState({
      devices: [...next.devices],
      boardNames: { ...next.boardNames },
      pinAliases: next.pinAliases ? { ...next.pinAliases } : {},
      setup: [...next.setup],
      actions: [...next.actions],
      onBoard: [...(next.onBoard ?? [])],
    })
    setSelection({ kind: 'overview' })
  }, [])

  /** Set custom label/alias for a pin on a device. */
  const setPinAlias = useCallback((ref: string, pin: number, alias: string) => {
    setState((current) => {
      const currentMap = current.pinAliases?.[ref] ?? {}
      const clean = alias.trim()
      const nextMap = { ...currentMap }
      if (clean) nextMap[String(pin)] = clean
      else delete nextMap[String(pin)]
      const nextPinAliases = { ...(current.pinAliases ?? {}) }
      if (Object.keys(nextMap).length > 0) nextPinAliases[ref] = nextMap
      else delete nextPinAliases[ref]
      return { ...current, pinAliases: nextPinAliases }
    })
  }, [])

  /** Configure pin mode via IO_OP_SET_MODE in setup. */
  const setPinMode = useCallback((ref: string, pin: number, mode: number | undefined) => {
    setState((current) => {
      const deviceId = ref.startsWith('board:') ? Number(ref.slice(6)) : current.devices.find((d) => d.id === ref)?.deviceId
      if (deviceId === undefined) return current
      const withoutMode = current.setup.filter((step) => !(step.device === deviceId && step.op === 'IO_OP_SET_MODE' && step.values.pin === pin))
      if (mode === undefined) {
        const withoutLevel = withoutMode.filter((step) => !(step.device === deviceId && step.op === 'IO_OP_SET_LEVEL' && step.values.pin === pin))
        return { ...current, setup: withoutLevel }
      }
      const newStep: ActionStep = {
        id: `step-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
        device: deviceId,
        op: 'IO_OP_SET_MODE',
        values: { pin, mode },
      }
      return { ...current, setup: [...withoutMode, newStep] }
    })
  }, [])

  /** Configure pin level via IO_OP_SET_LEVEL in setup. */
  const setPinLevel = useCallback((ref: string, pin: number, level: boolean | number | undefined) => {
    setState((current) => {
      const deviceId = ref.startsWith('board:') ? Number(ref.slice(6)) : current.devices.find((d) => d.id === ref)?.deviceId
      if (deviceId === undefined) return current
      const withoutLevel = current.setup.filter((step) => !(step.device === deviceId && step.op === 'IO_OP_SET_LEVEL' && step.values.pin === pin))
      if (level === undefined) return { ...current, setup: withoutLevel }
      const numLevel = typeof level === 'boolean' ? (level ? 1 : 0) : level
      const newStep: ActionStep = {
        id: `step-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
        device: deviceId,
        op: 'IO_OP_SET_LEVEL',
        values: { pin, level: numLevel },
      }
      return { ...current, setup: [...withoutLevel, newStep] }
    })
  }, [])

  /** The default settings of one device. */
  const setSetup = useCallback((deviceId: number, steps: readonly ActionStep[]) =>
    setState((current) => ({ ...current, setup: [...current.setup.filter((step) => step.device !== deviceId), ...steps] })), [])

  const addAction = useCallback(() => {
    const action = newAction(state.actions)
    setState((current) => ({ ...current, actions: [...current.actions, action] }))
    setSelection({ kind: 'action', id: action.id })
  }, [state.actions])

  const updateAction = useCallback((id: string, patch: Partial<Omit<ProjectAction, 'id'>>) =>
    setState((current) => ({ ...current, actions: current.actions.map((action) => (action.id === id ? { ...action, ...patch } : action)) })), [])

  const removeAction = useCallback((id: string) => {
    setState((current) => ({ ...current, actions: current.actions.filter((action) => action.id !== id) }))
    setSelection((current) => (current.kind === 'action' && current.id === id ? { kind: 'overview' } : current))
  }, [])

  /** Add a step to an action (creates one if none exist, or uses the specified/first action). */
  const addStepToAction = useCallback((actionId: string | undefined, step: ActionStep) => {
    setState((current) => {
      let targetId = actionId
      let actions = [...current.actions]
      if (!targetId || !actions.some((a) => a.id === targetId)) {
        if (actions.length === 0) {
          const created = newAction([])
          actions = [created]
          targetId = created.id
        } else {
          targetId = actions[0].id
        }
      }
      return {
        ...current,
        actions: actions.map((a) => (a.id === targetId ? { ...a, steps: [...a.steps, step] } : a)),
      }
    })
  }, [])

  const stored = useMemo((): StoredDeviceState => ({ devices: state.devices, setup: state.setup, actions: state.actions, onBoard: state.onBoard }), [state])

  /** What a save stores for the devices and actions, and what it removes (the ids the board lists, `stored`). */
  const records = useCallback((listed?: ReadonlySet<number>) => recordImages(stored, listed), [stored])
  /** The actions the board holds that this project did not put there: a save would replace them. */
  const foreign = useCallback((listed: ReadonlySet<number>) => foreignActions(stored, listed), [stored])
  /** A save went through: these are the actions on the board now. */
  const saved = useCallback(() => setState((current) => ({ ...current, onBoard: current.actions.map((action) => userActionId(action.actionId)) })), [])
  /** The board's records read: they replace the devices' installs, the default settings and the actions. */
  const synced = useCallback((read: ReadonlyMap<number, readonly Uint8Array[]>) =>
    setState((current) => ({ ...current, ...decodeRecords(read, current) })), [])

  const problems = useMemo(() => deviceProblems(state.devices), [state.devices])
  const stepProblems = useMemo(() => actionProblems(stored), [stored])

  return {
    ...state, state, selection, select, addDevice, updateDevice, removeDevice, renameBoardDevice, boardName, load, problems, stepProblems,
    setPinAlias, setPinMode, setPinLevel,
    setSetup, addAction, updateAction, removeAction, addStepToAction, records, foreign, saved, synced,
  }
}

export type DevicesWorkspace = ReturnType<typeof useDevicesWorkspace>

export { fromJson as devicesStateFromJson }
