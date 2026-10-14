import { useEffect, useMemo, useState } from 'react'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { useUndoHistory } from '../hooks/useUndoHistory'
import { boardSetup, defaultBleProfile, isBoardCharacteristic, isBoardService, profileFromJson } from '../domain/settings'
import type { BleCharacteristicSettings, BleGeneralSettings, BleProfile, BleServiceSettings } from '../domain/settings'

/** Auto-saved on every change, like the object tree. */
const STORAGE_KEY = 'runit.settings.ble'

export const GENERAL_ID = 'general'

const newId = (prefix: string): string => `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

export const uuidValue = (text: string | undefined): number => Number.parseInt((text ?? '').trim().replace(/^0x/i, ''), 16)

/** `0xAB` typed by hand → `0xAB` (up to four hex digits, upper case). */
export const normalizeUuid16 = (raw: string): string => `0x${raw.trim().replace(/^0x/i, '').replace(/[^0-9a-f]/gi, '').slice(0, 4).toUpperCase()}`

/** The same, padded to four digits (on blur). */
export const padUuid16 = (raw: string): string => `0x${normalizeUuid16(raw).slice(2).padStart(4, '0')}`

const isEditing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || !!target.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]'))

export type BleSelection =
  | { readonly kind: 'general' }
  | { readonly kind: 'service'; readonly service: BleServiceSettings }
  | { readonly kind: 'characteristic'; readonly service: BleServiceSettings; readonly characteristic: BleCharacteristicSettings }

/**
 * The GATT profile: the board's locked runIT service and the user's own services and characteristics (created at runtime by the
 * `ble` router). `active` turns the Delete key on only while this screen is the one shown.
 */
export function useBleSettings(active: boolean) {
  const history = useUndoHistory<BleProfile>(() => readStored(STORAGE_KEY, profileFromJson, defaultBleProfile()))
  const profile = history.present
  const [selectedId, setSelectedId] = useState<string | null>(() => profile.services[0]?.characteristics[0]?.id ?? GENERAL_ID)
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())

  usePersistEffect(STORAGE_KEY, profile)

  const select = (id: string) => setSelectedId(id)

  const toggleCollapsed = (serviceId: string) =>
    setCollapsed((current) => {
      const next = new Set(current)
      if (!next.delete(serviceId)) next.add(serviceId)
      return next
    })

  /** The profile the board's records build (a sync): the editor's own fields (general, names) stay. */
  const replaceProfile = (next: BleProfile) => history.record(next)

  const updateGeneral = (patch: Partial<BleGeneralSettings>) => history.record({ ...profile, general: { ...profile.general, ...patch } })

  /** The first UUID from `start` up that no service or characteristic uses (the board's included). */
  const freeUuid = (start: number): string => {
    const used = new Set(profile.services.flatMap((service) => [uuidValue(service.uuid), ...service.characteristics.map((char) => uuidValue(char.uuid))]))
    let value = start & 0xffff
    while (used.has(value) || value === 0) value = (value + 1) & 0xffff
    return `0x${value.toString(16).toUpperCase().padStart(4, '0')}`
  }

  const selectedItem = useMemo((): BleSelection | null => {
    if (!selectedId) return null
    if (selectedId === GENERAL_ID) return { kind: 'general' }
    for (const service of profile.services) {
      if (service.id === selectedId) return { kind: 'service', service }
      const characteristic = service.characteristics.find((char) => char.id === selectedId)
      if (characteristic) return { kind: 'characteristic', service, characteristic }
    }
    return null
  }, [profile, selectedId])

  const addService = () => {
    const service: BleServiceSettings = { id: newId('svc'), name: `Custom Service ${profile.services.length}`, uuid: freeUuid(0xee00), isPrimary: true, characteristics: [] }
    history.record({ ...profile, services: [...profile.services, service] })
    select(service.id)
  }

  const addCharacteristic = (serviceId?: string) => {
    const target = serviceId ?? (selectedItem && selectedItem.kind !== 'general' ? selectedItem.service.id : profile.services.find((service) => !isBoardService(service))?.id)
    const service = profile.services.find((entry) => entry.id === target)
    // The board's service is locked on the board (sys_ble_service_lock): user characteristics go in user services.
    if (!service || isBoardService(service)) return
    const char: BleCharacteristicSettings = {
      id: newId('chr'),
      name: `characteristic_${service.characteristics.length + 1}`,
      uuid: freeUuid((uuidValue(service.uuid) || 0xee00) + 1),
      write: false,
      notify: true,
      indicate: false,
      framed: false,
      txBufferSize: 96,
      rxBufferSize: 0,
    }
    history.record({ ...profile, services: profile.services.map((entry) => (entry.id === service.id ? { ...entry, characteristics: [...entry.characteristics, char] } : entry)) })
    select(char.id)
    setCollapsed((current) => {
      const next = new Set(current)
      next.delete(service.id)
      return next
    })
  }

  const updateService = (serviceId: string, patch: Partial<BleServiceSettings>) =>
    history.record({ ...profile, services: profile.services.map((service) => (service.id === serviceId ? { ...service, ...patch } : service)) })

  const updateCharacteristic = (serviceId: string, charId: string, patch: Partial<BleCharacteristicSettings>) =>
    history.record({ ...profile, services: profile.services.map((service) => (service.id === serviceId ? { ...service, characteristics: service.characteristics.map((char) => (char.id === charId ? { ...char, ...patch } : char)) } : service)) })

  const removeService = (serviceId: string) => {
    if (isBoardService(profile.services.find((service) => service.id === serviceId))) return
    const services = profile.services.filter((service) => service.id !== serviceId)
    history.record({ ...profile, services })
    if (selectedId === serviceId || profile.services.find((service) => service.id === serviceId)?.characteristics.some((char) => char.id === selectedId)) select(GENERAL_ID)
  }

  const removeCharacteristic = (serviceId: string, charId: string) => {
    const char = profile.services.find((service) => service.id === serviceId)?.characteristics.find((entry) => entry.id === charId)
    if (!char || isBoardCharacteristic(char)) return
    history.record({ ...profile, services: profile.services.map((service) => (service.id === serviceId ? { ...service, characteristics: service.characteristics.filter((entry) => entry.id !== charId) } : service)) })
    if (selectedId === charId) select(serviceId)
  }

  useEffect(() => {
    if (!active) return
    const onKey = (event: KeyboardEvent) => {
      if ((event.key !== 'Delete' && event.key !== 'Backspace') || event.repeat || event.altKey || event.ctrlKey || event.metaKey || isEditing(event.target)) return
      if (selectedItem?.kind === 'characteristic' && !isBoardCharacteristic(selectedItem.characteristic)) {
        event.preventDefault()
        removeCharacteristic(selectedItem.service.id, selectedItem.characteristic.id)
      } else if (selectedItem?.kind === 'service' && !isBoardService(selectedItem.service)) {
        event.preventDefault()
        removeService(selectedItem.service.id)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  })

  return {
    profile,
    selectedId,
    selectedItem,
    select,
    collapsed,
    toggleCollapsed,
    updateGeneral,
    addService,
    replaceProfile,
    addCharacteristic,
    updateService,
    updateCharacteristic,
    removeService,
    removeCharacteristic,
    /** The firmware's own limit on a characteristic's buffer, shown next to the field. */
    frameMax: boardSetup().limits.frameMax,
    canUndo: history.canUndo,
    canRedo: history.canRedo,
    undo: history.undo,
    redo: history.redo,
  }
}

export type BleSettings = ReturnType<typeof useBleSettings>
