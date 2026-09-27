import { useEffect, useMemo, useState } from 'react'
import {
  ArrowLeft,
  ChevronDown,
  ChevronRight,
  FolderPlus,
  Layers,
  Plus,
  Radio,
  Search,
  Sliders,
  Trash2,
  X,
} from 'lucide-react'
import { EditableField } from './components/EditableField'
import { ListableField } from './components/ListableField'
import { TreeSlab } from './components/TreeSlab'
import { runitStreamCatalog } from './domain/descriptors'
import { parseSettings } from './domain/project'
import type { BleCharacteristicSettings, BleGeneralSettings, BleProfile, BleServiceSettings } from './domain/project'
import { defaultProjectSettings, isBoardCharacteristic, isBoardService, refreshSettings, streamsOnCharacteristic } from './domain/settings'

// Editor state = the project's settings section (domain/project).
export type BleCharacteristic = BleCharacteristicSettings
export type BleService = BleServiceSettings
export type { BleGeneralSettings, BleProfile }

/** Auto-saved on every change, like the object tree; also saved in the project file. */
const STORAGE_KEY = 'runit.ble.profile'

const board = runitStreamCatalog().board
const hex16 = (value: number): string => `0x${value.toString(16).padStart(4, '0').toUpperCase()}`
const uuidValue = (text: string | undefined): number => Number.parseInt((text ?? '').trim().replace(/^0x/i, ''), 16)

export const isSystemBleService = (service?: BleService | null): boolean => isBoardService(service)
export const isSystemBleChar = (char?: BleCharacteristic | null): boolean => isBoardCharacteristic(char)

/** The GATT profile a board starts with (what it holds after a restart), from the firmware descriptors. */
export const defaultBleProfile: BleProfile = defaultProjectSettings().ble

/** The saved profile, the board's own entries refreshed from the descriptors (refreshSettings). */
const loadBleProfile = (): BleProfile => {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) return refreshSettings(parseSettings({ ble: JSON.parse(raw), connectors: [] }, 'ble-profile')).ble
  } catch {
    /* Storage unavailable or unreadable: start from the defaults. */
  }
  return defaultBleProfile
}

const newId = (prefix: string) => `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

const normalize = (s: string) => s.replace(/\s+/g, '').toLowerCase()

export const matchesBleSearch = (text: string, query: string): boolean => {
  const q = query.trim()
  if (!q) return false
  const qNorm = normalize(q)
  if (!qNorm) return false
  return normalize(text).includes(qNorm)
}

export const normalizeUuid16 = (raw: string): string => {
  let cleaned = raw.trim()
  if (cleaned.startsWith('0x') || cleaned.startsWith('0X')) {
    cleaned = cleaned.slice(2)
  }
  const hex = cleaned.replace(/[^0-9a-fA-F]/g, '').slice(0, 4).toUpperCase()
  return `0x${hex}`
}

export const padUuid16 = (raw: string): string => {
  const norm = normalizeUuid16(raw)
  const hex = norm.slice(2).padStart(4, '0')
  return `0x${hex}`
}

export function useBleSettingsWorkspace(onSelect?: (id: string) => void) {
  const [initial] = useState(loadBleProfile)
  const [profile, setProfile] = useState<BleProfile>(initial)
  const [history, setHistory] = useState<BleProfile[]>([initial])
  const [historyIndex, setHistoryIndex] = useState(0)
  const [justApplied, setJustApplied] = useState(false)
  const [selectedId, setSelectedId] = useState<string | null>(defaultBleProfile.services[0]?.characteristics[0]?.id ?? null)
  const [collapsedServices, setCollapsedServices] = useState<Set<string>>(new Set())

  const canUndo = historyIndex > 0
  const canRedo = historyIndex < history.length - 1

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(profile))
    } catch {
      /* The project file keeps it. */
    }
  }, [profile])

  const pushState = (next: BleProfile) => {
    setProfile(next)
    setHistory((prev) => [...prev.slice(0, historyIndex + 1), next])
    setHistoryIndex((prev) => prev + 1)
  }

  /** Replace the profile (project opened or recovered); undo history starts over. */
  const load = (next: BleProfile) => {
    setProfile(next)
    setHistory([next])
    setHistoryIndex(0)
    setSelectedId(next.services[0]?.characteristics[0]?.id ?? null)
  }

  const select = (id: string) => {
    setSelectedId(id)
    onSelect?.(id)
  }

  const undo = () => {
    if (!canUndo) return
    const nextIndex = historyIndex - 1
    setHistoryIndex(nextIndex)
    setProfile(history[nextIndex])
  }

  const redo = () => {
    if (!canRedo) return
    const nextIndex = historyIndex + 1
    setHistoryIndex(nextIndex)
    setProfile(history[nextIndex])
  }

  /** Flash the apply button after the board took the settings. */
  const markApplied = () => {
    setJustApplied(true)
    setTimeout(() => setJustApplied(false), 2200)
  }

  const toggleServiceCollapsed = (serviceId: string) => {
    setCollapsedServices((curr) => {
      const next = new Set(curr)
      if (next.has(serviceId)) next.delete(serviceId)
      else next.add(serviceId)
      return next
    })
  }

  const updateGeneral = (patch: Partial<BleGeneralSettings>) => {
    const next: BleProfile = {
      ...profile,
      general: { ...profile.general, ...patch },
    }
    pushState(next)
  }

  const addService = () => {
    const svcIndex = profile.services.length + 1
    const hexNum = (0xffe0 + svcIndex) & 0xffff
    const hex = hexNum.toString(16).toUpperCase().padStart(4, '0')
    const newService: BleService = {
      id: newId('svc'),
      name: `Custom Service ${svcIndex}`,
      uuid: `0x${hex}`,
      isPrimary: true,
      advertised: false,
      characteristics: [],
    }
    const next: BleProfile = {
      ...profile,
      services: [...profile.services, newService],
    }
    pushState(next)
    select(newService.id)
  }

  const addCharacteristic = (serviceId?: string) => {
    const targetServiceId =
      serviceId ??
      (selectedItem?.kind === 'service'
        ? selectedItem.service.id
        : selectedItem?.kind === 'characteristic'
          ? selectedItem.service.id
          : profile.services[0]?.id)
    if (!targetServiceId) return
    const service = profile.services.find((s) => s.id === targetServiceId)
    if (!service) return

    const charIndex = service.characteristics.length + 1
    const baseUuidNum = parseInt(service.uuid, 16) || 0xffe0
    const charUuidHex = (baseUuidNum + charIndex).toString(16).toUpperCase().padStart(4, '0')

    const newChar: BleCharacteristic = {
      id: newId('chr'),
      name: `Characteristic_${charIndex}`,
      uuid: `0x${charUuidHex}`,
      read: true,
      write: false,
      writeNoResponse: false,
      notify: false,
      indicate: false,
      txBufferSize: 0,
      rxBufferSize: 0,
      format: 'U8',
      initialValue: '0',
    }

    const nextServices = profile.services.map((s) => {
      if (s.id !== targetServiceId) return s
      return { ...s, characteristics: [...s.characteristics, newChar] }
    })

    pushState({ ...profile, services: nextServices })
    select(newChar.id)
    setCollapsedServices((curr) => {
      const next = new Set(curr)
      next.delete(targetServiceId)
      return next
    })
  }

  const updateService = (serviceId: string, patch: Partial<BleService>) => {
    const nextServices = profile.services.map((s) => (s.id === serviceId ? { ...s, ...patch } : s))
    pushState({ ...profile, services: nextServices })
  }

  const updateCharacteristic = (serviceId: string, charId: string, patch: Partial<BleCharacteristic>) => {
    const nextServices = profile.services.map((s) => {
      if (s.id !== serviceId) return s
      return {
        ...s,
        characteristics: s.characteristics.map((c) => (c.id === charId ? { ...c, ...patch } : c)),
      }
    })
    pushState({ ...profile, services: nextServices })
  }

  const removeService = (serviceId: string) => {
    const target = profile.services.find((s) => s.id === serviceId)
    if (target && isSystemBleService(target)) return
    const nextServices = profile.services.filter((s) => s.id !== serviceId)
    pushState({ ...profile, services: nextServices })
    if (selectedId === serviceId) {
      select(nextServices[0]?.id ?? 'general')
    }
  }

  const removeCharacteristic = (serviceId: string, charId: string) => {
    const service = profile.services.find((s) => s.id === serviceId)
    const char = service?.characteristics.find((c) => c.id === charId)
    if (char && isSystemBleChar(char)) return
    const nextServices = profile.services.map((s) => {
      if (s.id !== serviceId) return s
      return {
        ...s,
        characteristics: s.characteristics.filter((c) => c.id !== charId),
      }
    })
    pushState({ ...profile, services: nextServices })
    if (selectedId === charId) {
      select(serviceId)
    }
  }

  const selectedItem = useMemo(() => {
    if (!selectedId) return null
    if (selectedId === 'general') {
      return { kind: 'general' as const }
    }
    for (const service of profile.services) {
      if (service.id === selectedId) {
        return { kind: 'service' as const, service }
      }
      for (const char of service.characteristics) {
        if (char.id === selectedId) {
          return { kind: 'characteristic' as const, service, characteristic: char }
        }
      }
    }
    return null
  }, [profile, selectedId])

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key !== 'Delete' && event.key !== 'Backspace') return
      if (event.repeat || event.altKey || event.ctrlKey || event.metaKey) return
      const target = event.target
      if (
        target instanceof HTMLElement &&
        (target.isContentEditable || target.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]'))
      ) {
        return
      }
      if (selectedItem?.kind === 'characteristic') {
        if (isSystemBleChar(selectedItem.characteristic)) return
        event.preventDefault()
        removeCharacteristic(selectedItem.service.id, selectedItem.characteristic.id)
      } else if (selectedItem?.kind === 'service') {
        if (isSystemBleService(selectedItem.service)) return
        event.preventDefault()
        removeService(selectedItem.service.id)
      }
    }
    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [selectedItem, removeCharacteristic, removeService])

  return {
    profile,
    selectedId,
    setSelectedId,
    select,
    selectedItem,
    canUndo,
    canRedo,
    undo,
    redo,
    justApplied,
    markApplied,
    load,
    collapsedServices,
    toggleServiceCollapsed,
    updateGeneral,
    addService,
    addCharacteristic,
    updateService,
    updateCharacteristic,
    removeService,
    removeCharacteristic,
  }
}

export type BleSettingsWorkspace = ReturnType<typeof useBleSettingsWorkspace>

export function BleSettingsPalette({
  workspace: w,
  onBackToSettings,
}: {
  workspace: BleSettingsWorkspace
  onBackToSettings?: () => void
}) {
  const [searchQuery, setSearchQuery] = useState('')

  const isCharMatch = (char: BleCharacteristic) => {
    if (!searchQuery.trim()) return false
    return matchesBleSearch(char.name, searchQuery) || matchesBleSearch(char.uuid, searchQuery)
  }

  const isServiceMatch = (service: BleService) => {
    if (!searchQuery.trim()) return false
    return matchesBleSearch(service.name, searchQuery) || matchesBleSearch(service.uuid, searchQuery)
  }

  const serviceHasMatchingDescendants = (service: BleService) => {
    if (!searchQuery.trim()) return false
    return service.characteristics.some(isCharMatch)
  }

  const isServiceVisible = (service: BleService) => {
    if (!searchQuery.trim()) return true
    return isServiceMatch(service) || serviceHasMatchingDescendants(service)
  }

  const isCharVisible = (service: BleService, char: BleCharacteristic) => {
    if (!searchQuery.trim()) return true
    if (isServiceMatch(service)) return true
    return isCharMatch(char)
  }

  const filteredServices = w.profile.services.filter(isServiceVisible)

  return (
    <div className="object-tree-palette ble-palette">
      {onBackToSettings && (
        <div className="ble-palette-back-bar">
          <button
            type="button"
            className="settings-back-btn"
            onClick={onBackToSettings}
            title="Back to all settings"
          >
            <ArrowLeft aria-hidden="true" />
            <span>All Settings</span>
          </button>
        </div>
      )}

      {/* BLE General pinned above search & GATT profile */}
      <div className="ble-palette-general-top">
        <button
          className={`ble-subtab-button ${w.selectedId === 'general' ? 'selected' : ''}`}
          onClick={() => w.select('general')}
          title="Open BLE General Settings"
        >
          <Sliders className="ble-subtab-icon" aria-hidden="true" />
          <span className="ble-subtab-title">BLE General</span>
        </button>
      </div>

      <div className="object-tree-search-bar">
        <Search className="search-icon" aria-hidden="true" />
        <input
          type="text"
          placeholder="Search BLE services & chars..."
          aria-label="Search BLE services & chars"
          value={searchQuery}
          onChange={(e) => setSearchQuery(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Escape') setSearchQuery('')
          }}
        />
        {searchQuery && (
          <button
            type="button"
            className="search-clear-btn"
            title="Clear search"
            aria-label="Clear search"
            onClick={() => setSearchQuery('')}
          >
            <X aria-hidden="true" />
          </button>
        )}
      </div>

      <div className="object-tree-heading">
        <span className="ble-palette-title">GATT Profile</span>
        <button
          title="Add Service"
          aria-label="Add Service"
          onClick={w.addService}
        >
          <FolderPlus aria-hidden="true" />
        </button>
        <button
          title="Add Characteristic to selected Service"
          aria-label="Add Characteristic to selected Service"
          onClick={() => w.addCharacteristic()}
        >
          <Plus aria-hidden="true" />
        </button>
        {w.selectedItem &&
          ((w.selectedItem.kind === 'characteristic' && !isSystemBleChar(w.selectedItem.characteristic)) ||
            (w.selectedItem.kind === 'service' && !isSystemBleService(w.selectedItem.service))) && (
          <button
            type="button"
            className="ble-delete-heading-btn"
            title={w.selectedItem.kind === 'characteristic' ? 'Delete Characteristic (Del)' : 'Delete Service (Del)'}
            aria-label="Delete selected item"
            onClick={() => {
              if (w.selectedItem?.kind === 'characteristic') {
                w.removeCharacteristic(w.selectedItem.service.id, w.selectedItem.characteristic.id)
              } else if (w.selectedItem?.kind === 'service') {
                w.removeService(w.selectedItem.service.id)
              }
            }}
          >
            <Trash2 aria-hidden="true" />
          </button>
        )}
      </div>

      <div className="object-tree-scroll" role="navigation" aria-label="BLE GATT Services and Characteristics">
        {filteredServices.map((service) => {
          const isSvcMatch = isServiceMatch(service)
          const isSelected = w.selectedId === service.id
          const isCollapsed = w.collapsedServices.has(service.id) && !serviceHasMatchingDescendants(service)
          const visibleChars = service.characteristics.filter((c) => isCharVisible(service, c))

          return (
            <div key={service.id} className="ble-service-group">
              <TreeSlab
                isFolder
                twoRowOnNarrow
                disclosurePosition="left"
                selected={isSelected}
                isMatch={isSvcMatch}
                onClick={() => w.select(service.id)}
                onDoubleClick={() => w.toggleServiceCollapsed(service.id)}
                icon={
                  <span className="object-type-icon folder" title="BLE GATT Service">
                    <Layers aria-hidden="true" />
                  </span>
                }
                label={service.name}
                badges={
                  <>
                    {service.advertised && <span className="ble-adv-tag">ADV</span>}
                    {isSystemBleService(service) && <span className="conn-system-label">SYS</span>}
                    <span className="ble-uuid-chip">{service.uuid}</span>
                  </>
                }
                disclosure={
                  <button
                    type="button"
                    className="tree-slab-disclosure"
                    aria-label={`${isCollapsed ? 'Expand' : 'Collapse'} ${service.name}`}
                    onClick={(e) => {
                      e.stopPropagation()
                      w.toggleServiceCollapsed(service.id)
                    }}
                  >
                    {isCollapsed ? <ChevronRight /> : <ChevronDown />}
                  </button>
                }
                actions={
                  !isSystemBleService(service) && (
                    <button
                      type="button"
                      className="tree-slab-action ble-tree-delete-btn"
                      title="Delete Service (Del)"
                      aria-label={`Delete ${service.name}`}
                      onClick={(e) => {
                        e.stopPropagation()
                        w.removeService(service.id)
                      }}
                    >
                      <Trash2 aria-hidden="true" />
                    </button>
                  )
                }
              />

              {!isCollapsed && (
                <div className="object-tree-children depth-1">
                  {visibleChars.map((char) => {
                    const isCCharMatch = isCharMatch(char)
                    const isCharSelected = w.selectedId === char.id

                    return (
                      <TreeSlab
                        key={char.id}
                        selected={isCharSelected}
                        twoRowOnNarrow
                        isMatch={isCCharMatch}
                        onClick={() => w.select(char.id)}
                        icon={
                          <span className="object-type-icon text" title="BLE Characteristic">
                            <Radio aria-hidden="true" />
                          </span>
                        }
                        label={char.name}
                        badges={
                          <>
                            <div className="ble-prop-badges">
                              {char.read && <span className="ble-badge r" title="Read">R</span>}
                              {char.write && <span className="ble-badge w" title="Write">W</span>}
                              {char.writeNoResponse && <span className="ble-badge wr" title="Write Without Response">WNR</span>}
                              {char.notify && <span className="ble-badge n" title="Notify">N</span>}
                              {char.indicate && <span className="ble-badge i" title="Indicate">I</span>}
                            </div>
                            {isSystemBleChar(char) && <span className="conn-system-label">SYS</span>}
                            <span className="ble-uuid-chip">{char.uuid}</span>
                          </>
                        }
                        actions={
                          !isSystemBleChar(char) && (
                            <button
                              type="button"
                              className="tree-slab-action ble-tree-delete-btn"
                              title="Delete Characteristic (Del)"
                              aria-label={`Delete ${char.name}`}
                              onClick={(e) => {
                                e.stopPropagation()
                                w.removeCharacteristic(service.id, char.id)
                              }}
                            >
                              <Trash2 aria-hidden="true" />
                            </button>
                          )
                        }
                      />
                    )
                  })}
                  {!visibleChars.length && (
                    <div className="ble-empty-chars">
                      No characteristics. Click + in heading to add.
                    </div>
                  )}
                </div>
              )}
            </div>
          )
        })}

        {!filteredServices.length && (
          <p className="object-tree-empty">
            {searchQuery ? `No BLE services matching "${searchQuery}"` : 'No BLE services configured yet.'}
          </p>
        )}
      </div>
    </div>
  )
}

export interface ConnectorStreamDef {
  id: string
  name: string
  alias: string
  header: string
  connector: string
  description: string
  direction: 'in' | 'out' | 'inout'
  defaultUuid: string
}

/** The board's streams (streams.generated.json), with the direction and characteristic its default bindings give them. */
export const CONNECTOR_STREAMS: ConnectorStreamDef[] = runitStreamCatalog().streams.map((stream) => {
  const bindings = board.bindings.filter((binding) => binding.connectorId === stream.connectorId)
  const tx = bindings.some((binding) => binding.direction === 'tx')
  const rx = bindings.some((binding) => binding.direction === 'rx')
  const uuid = stream.ble?.write ?? stream.ble?.notify
  return {
    id: stream.name,
    name: stream.alias,
    alias: `${stream.alias} (0x${stream.header.toString(16).padStart(2, '0').toUpperCase()})`,
    header: `0x${stream.header.toString(16).padStart(2, '0').toUpperCase()}`,
    connector: stream.connector,
    description: stream.description,
    direction: tx && rx ? 'inout' : rx ? 'in' : 'out',
    defaultUuid: uuid === undefined ? '' : hex16(uuid),
  }
})

/** The stream the board carries on this characteristic by default (the first, if several share it). */
export const defaultStreamForUuid = (uuid: string): string | undefined => streamsOnCharacteristic(uuidValue(uuid))[0]

export function BleSettingsEditor({ workspace: w }: { workspace: BleSettingsWorkspace }) {
  if (w.selectedId === 'general') {
    const gen = w.profile.general
    return (
      <div className="object-editor ble-editor">
        <div className="ble-general-header">
          <Sliders aria-hidden="true" />
          <h1>BLE General Settings</h1>
        </div>

        <div className="ble-editor-grid">
          {/* Device & Advertising Settings */}
          <div className="ble-card">
            <h3>Device Identity &amp; Advertising</h3>
            <div className="ble-form-row">
              <label>Broadcast Device Name</label>
              <input
                type="text"
                className="ble-input-field"
                value={gen.deviceName}
                onChange={(e) => w.updateGeneral({ deviceName: e.target.value })}
                placeholder="runIT-Device"
              />
            </div>

            <div className="ble-two-col-grid">
              <div className="ble-form-row">
                <label>Advertising Interval (ms)</label>
                <input
                  type="number"
                  min="20"
                  max="10240"
                  className="ble-input-field"
                  value={gen.advIntervalMs}
                  onChange={(e) => w.updateGeneral({ advIntervalMs: Math.max(20, Number(e.target.value) || 20) })}
                />
              </div>

              <div className="ble-form-row">
                <label>Fast Advertising Timeout (s)</label>
                <input
                  type="number"
                  min="0"
                  max="3600"
                  className="ble-input-field"
                  value={gen.advFastTimeoutSec}
                  onChange={(e) => w.updateGeneral({ advFastTimeoutSec: Math.max(0, Number(e.target.value) || 0) })}
                />
              </div>
            </div>
          </div>

          {/* Connection Parameters & RF */}
          <div className="ble-card">
            <h3>Connection Parameters &amp; RF</h3>
            <div className="ble-two-col-grid">
              <div className="ble-form-row">
                <label>Min Connection Interval (ms)</label>
                <input
                  type="number"
                  min="7.5"
                  max="4000"
                  step="1.25"
                  className="ble-input-field"
                  value={gen.minConnIntervalMs}
                  onChange={(e) => w.updateGeneral({ minConnIntervalMs: Number(e.target.value) || 15 })}
                />
              </div>

              <div className="ble-form-row">
                <label>Max Connection Interval (ms)</label>
                <input
                  type="number"
                  min="7.5"
                  max="4000"
                  step="1.25"
                  className="ble-input-field"
                  value={gen.maxConnIntervalMs}
                  onChange={(e) => w.updateGeneral({ maxConnIntervalMs: Number(e.target.value) || 30 })}
                />
              </div>

              <div className="ble-form-row">
                <label>Supervision Timeout (ms)</label>
                <input
                  type="number"
                  min="100"
                  max="32000"
                  className="ble-input-field"
                  value={gen.supervisionTimeoutMs}
                  onChange={(e) => w.updateGeneral({ supervisionTimeoutMs: Number(e.target.value) || 5000 })}
                />
              </div>

              <div className="ble-form-row">
                <label>Preferred MTU Size (bytes)</label>
                <input
                  type="number"
                  min="23"
                  max="517"
                  className="ble-input-field"
                  value={gen.mtuSize}
                  onChange={(e) => w.updateGeneral({ mtuSize: Math.min(517, Math.max(23, Number(e.target.value) || 23)) })}
                />
                <span className="ble-field-hint">Range 23 – 517 B. ATT MTU exchange requested upon client connection.</span>
              </div>
            </div>
          </div>

          {/* BLE Buffer Sizes & ATT Limits */}
          <div className="ble-card">
            <div className="card-header-badge">
              <Layers aria-hidden="true" />
              <h3>Buffer Sizes &amp; Memory Allocation</h3>
            </div>
            <p className="card-subhead">
              Configured static buffers and maximum throughput limits for Bluetooth Low Energy GATT transport.
            </p>

            <div className="ble-two-col-grid">
              <div className="conn-buffer-stat-box">
                <span className="stat-title">Maximum MTU</span>
                <span className="stat-val">{gen.mtuSize} B</span>
                <span className="stat-hint">Negotiated connection packet size limit.</span>
              </div>

              <div className="conn-buffer-stat-box">
                <span className="stat-title">Max Transmission Payload</span>
                <span className="stat-val">{Math.max(20, gen.mtuSize - 3)} B</span>
                <span className="stat-hint">Maximum payload capacity per packet.</span>
              </div>

              <div className="conn-buffer-stat-box">
                <span className="stat-title">Characteristic Buffer</span>
                <span className="stat-val">512 B</span>
                <span className="stat-hint">Maximum characteristic attribute capacity.</span>
              </div>

              <div className="conn-buffer-stat-box">
                <span className="stat-title">Data Stream Payload</span>
                <span className="stat-val">{Math.max(0, Math.min(511, gen.mtuSize - 4))} B</span>
                <span className="stat-hint">Usable stream throughput per transmission.</span>
              </div>
            </div>
          </div>

          {/* Security & Passkey Management */}
          <div className="ble-card">
            <h3>Security &amp; PIN / Passkey Management</h3>
            <div className="ble-form-row">
              <label>Pairing Security Level</label>
              <select
                className="ble-select-field"
                value={gen.securityMode}
                onChange={(e) => w.updateGeneral({ securityMode: e.target.value as BleGeneralSettings['securityMode'] })}
              >
                <option value="just_works">No Security (Just Works)</option>
                <option value="passkey">Static 6-Digit PIN / Passkey</option>
                <option value="mitm">Encrypted MITM (Man-In-The-Middle Protection)</option>
              </select>
            </div>

            {gen.securityMode !== 'just_works' && (
              <div className="ble-form-row">
                <label>Static 6-Digit Passkey PIN</label>
                <input
                  type="text"
                  maxLength={6}
                  className="ble-input-field"
                  value={gen.passkeyPin}
                  onChange={(e) => w.updateGeneral({ passkeyPin: e.target.value.replace(/\D/g, '') })}
                  placeholder="123456"
                />
              </div>
            )}
          </div>
        </div>
      </div>
    )
  }

  // Main editor surface: exactly matching main object tab (one block per item, 0xXXXX 16-bit format, no bottom row)
  return (
    <div className="object-editor ble-editor">
      <div className="object-main-list">
        {w.profile.services.map((service) => {
          const isSvcSelected = w.selectedId === service.id
          const isCollapsed = w.collapsedServices.has(service.id)

          return (
            <div key={service.id} className="object-main-entry depth-0">
              {/* Service Card */}
              <div
                className={`object-main-card folder ${isSvcSelected ? 'selected' : ''}`}
                onClick={() => w.select(service.id)}
              >
                <div className="object-main-row object-main-top">
                  <button
                    type="button"
                    className="object-main-disclosure"
                    title={isCollapsed ? 'Expand service' : 'Collapse service'}
                    aria-label={isCollapsed ? `Expand ${service.name}` : `Collapse ${service.name}`}
                    onClick={(e) => {
                      e.stopPropagation()
                      w.toggleServiceCollapsed(service.id)
                    }}
                  >
                    {isCollapsed ? <ChevronRight /> : <ChevronDown />}
                  </button>
                  <Layers className="object-type-icon folder" aria-hidden="true" />
                  <EditableField
                    aria-label={`Name of ${service.name}`}
                    value={service.name}
                    placeholder="Service Name"
                    disabled={isSystemBleService(service)}
                    title={isSystemBleService(service) ? 'Protected system service' : 'Service Name'}
                    iconTitle={isSystemBleService(service) ? 'Protected system service (Read-only)' : 'Editable Service Name'}
                    onClick={(e) => e.stopPropagation()}
                    onChange={(val) => w.updateService(service.id, { name: val })}
                  />
                  <EditableField
                    aria-label={`UUID of ${service.name}`}
                    value={service.uuid}
                    placeholder="0xFFE0"
                    maxLength={6}
                    disabled={isSystemBleService(service)}
                    title={isSystemBleService(service) ? 'Protected system UUID (Read-only)' : '16-bit Service UUID (0xXXXX)'}
                    iconTitle={isSystemBleService(service) ? 'Protected system UUID (Read-only)' : 'Editable UUID'}
                    onClick={(e) => e.stopPropagation()}
                    onChange={(val) => w.updateService(service.id, { uuid: normalizeUuid16(val) })}
                    onBlur={(e) => w.updateService(service.id, { uuid: padUuid16(e.target.value) })}
                  />
                  <span className="object-main-kind">
                    {service.isPrimary ? 'Primary Service' : 'Service'}
                  </span>
                  <div className="ble-col-action">
                    {isSystemBleService(service) ? (
                      <span className="conn-system-label ble-sys-badge" title="Core System Service">SYS</span>
                    ) : (
                      <button
                        type="button"
                        className="object-main-delete"
                        title="Delete service"
                        aria-label={`Delete ${service.name}`}
                        onClick={(e) => {
                          e.stopPropagation()
                          w.removeService(service.id)
                        }}
                      >
                        <Trash2 />
                      </button>
                    )}
                  </div>
                </div>
                <div className="object-main-row object-main-bottom">
                  <span>
                    {service.characteristics.length}{' '}
                    {service.characteristics.length === 1 ? 'characteristic' : 'characteristics'}
                  </span>
                  {service.advertised && <span className="ble-adv-tag">Advertised in broadcast</span>}
                </div>
              </div>

              {/* Characteristics Indented List: single sleek row per characteristic */}
              {!isCollapsed && (
                <div className="object-main-children">
                  {service.characteristics.map((char) => {
                    const isCharSelected = w.selectedId === char.id

                    return (
                      <div key={char.id} className="object-main-entry depth-1">
                        <div
                          className={`object-main-card is-compact ble-char-card ${isCharSelected ? 'selected' : ''}`}
                          onClick={() => w.select(char.id)}
                        >
                          <div className="object-main-row object-main-top">
                            <Radio className="object-type-icon text" aria-hidden="true" />
                            <EditableField
                              aria-label={`Name of ${char.name}`}
                              value={char.name}
                              placeholder="Characteristic Name"
                              disabled={isSystemBleChar(char)}
                              title={isSystemBleChar(char) ? 'Protected system characteristic' : 'Characteristic Name'}
                              iconTitle={isSystemBleChar(char) ? 'Protected system characteristic (Read-only)' : 'Editable Characteristic Name'}
                              onClick={(e) => e.stopPropagation()}
                              onChange={(val) => w.updateCharacteristic(service.id, char.id, { name: val })}
                            />
                            <EditableField
                              aria-label={`UUID of ${char.name}`}
                              value={char.uuid}
                              placeholder="0xFFE1"
                              maxLength={6}
                              disabled={isSystemBleChar(char)}
                              title={isSystemBleChar(char) ? 'Protected system UUID (Read-only)' : '16-bit Characteristic UUID (0xXXXX)'}
                              iconTitle={isSystemBleChar(char) ? 'Protected system UUID (Read-only)' : 'Editable UUID'}
                              onClick={(e) => e.stopPropagation()}
                              onChange={(val) =>
                                w.updateCharacteristic(service.id, char.id, { uuid: normalizeUuid16(val) })
                              }
                              onBlur={(e) =>
                                w.updateCharacteristic(service.id, char.id, { uuid: padUuid16(e.target.value) })
                              }
                            />
                            <ListableField
                              aria-label={`Format of ${char.name}`}
                              value={char.format}
                              options={['RAW', 'STR', 'U8', 'U16', 'U32', 'I32', 'F']}
                              disabled={isSystemBleChar(char)}
                              title={isSystemBleChar(char) ? 'Protected system format (Read-only)' : 'Click to override format'}
                              iconTitle={isSystemBleChar(char) ? 'Protected format' : 'Select format'}
                              onClick={(e) => e.stopPropagation()}
                              onChange={(val) =>
                                w.updateCharacteristic(service.id, char.id, {
                                  format: val as BleCharacteristic['format'],
                                })
                              }
                            />
                            <div className="ble-col-action">
                              {isSystemBleChar(char) ? (
                                <span className="conn-system-label ble-sys-badge" title="Core System Characteristic">SYS</span>
                              ) : (
                                <button
                                  type="button"
                                  className="object-main-delete"
                                  title="Delete characteristic"
                                  aria-label={`Delete ${char.name}`}
                                  onClick={(e) => {
                                    e.stopPropagation()
                                    w.removeCharacteristic(service.id, char.id)
                                  }}
                                >
                                  <Trash2 />
                                </button>
                              )}
                            </div>
                          </div>
                        </div>
                      </div>
                    )
                  })}

                  <button
                    type="button"
                    className="ble-add-char-inline-btn"
                    onClick={() => w.addCharacteristic(service.id)}
                  >
                    <Plus aria-hidden="true" />
                    <span>Add Characteristic</span>
                  </button>
                </div>
              )}
            </div>
          )
        })}

        {!w.profile.services.length && (
          <div className="object-editor-empty">
            <p>No BLE GATT services yet.</p>
            <div>
              <button onClick={w.addService}>Add Service</button>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}

export function BleDetails({
  workspace: w,
  onJump,
}: {
  workspace: BleSettingsWorkspace
  onJump?: (id: string) => void
}) {
  const item = w.selectedItem

  if (!item || item.kind === 'general') {
    return (
      <div className="object-details ble-details">
        <p className="object-tree-empty" style={{ padding: '24px 16px' }}>
          No additional inspector details for general settings.
        </p>
      </div>
    )
  }

  if (item.kind === 'service') {
    const { service } = item
    return (
      <div className="object-details ble-details">
        <div className="object-details-header">
          <Layers className="object-check-eye" />
          <h2>{service.name}</h2>
          {isSystemBleService(service) && <span className="conn-system-label">SYS</span>}
          <span className="ble-uuid-chip">{service.uuid}</span>
          {!isSystemBleService(service) && (
            <button
              type="button"
              className="object-main-delete"
              title="Delete service"
              aria-label={`Delete ${service.name}`}
              onClick={() => w.removeService(service.id)}
            >
              <Trash2 />
            </button>
          )}
        </div>

        <div className="object-details-section">
          <h3>Service Options</h3>
          <div className="object-details-toggles">
            <label className="object-details-check">
              <input
                type="checkbox"
                checked={service.isPrimary}
                onChange={(e) => w.updateService(service.id, { isPrimary: e.target.checked })}
              />
              <span className="object-check-box" aria-hidden="true" />
              <span>Primary Service</span>
            </label>
            <label className="object-details-check">
              <input
                type="checkbox"
                checked={service.advertised}
                onChange={(e) => w.updateService(service.id, { advertised: e.target.checked })}
              />
              <span className="object-check-box" aria-hidden="true" />
              <span>Advertise in BLE Packets</span>
            </label>
          </div>
        </div>

        <div className="object-details-section">
          <h3>Characteristics ({service.characteristics.length})</h3>
          <div className="ble-related-chars">
            {service.characteristics.map((c) => (
              <button
                key={c.id}
                className="ble-related-char-button"
                onClick={() => {
                  w.select(c.id)
                  onJump?.(c.id)
                }}
              >
                <Radio className="object-type-icon text" />
                <span className="ble-related-name">{c.name}</span>
                <span className="ble-uuid-chip">{c.uuid}</span>
                <ChevronRight />
              </button>
            ))}
            {!service.characteristics.length && <p>No characteristics in this service.</p>}
          </div>
        </div>

        <div className="object-details-section">
          <h3>Relations to connectors</h3>
          <div className="ble-connector-summary-list">
            {service.characteristics.map((c) => {
              const streamId = c.connectorStream || defaultStreamForUuid(c.uuid)
              const stream = CONNECTOR_STREAMS.find((s) => s.id === streamId)
              return (
                <div key={c.id} className="ble-connector-row">
                  <div className="ble-connector-char-info">
                    <span className="ble-connector-char-name">{c.name} ({c.uuid})</span>
                    <span className="ble-connector-target">
                      {stream ? stream.alias : 'Custom / Unassigned'}
                    </span>
                  </div>
                  {stream && (
                    <span className={`ble-direction-tag ${stream.direction}`}>
                      {stream.direction === 'in' ? 'IN' : stream.direction === 'out' ? 'OUT' : 'IN/OUT'}
                    </span>
                  )}
                </div>
              )
            })}
          </div>
        </div>
      </div>
    )
  }

  // Characteristic Details (Properties, Default Value & Relations to connectors)
  const { service, characteristic: char } = item
  const boundStreamId = char.connectorStream || defaultStreamForUuid(char.uuid)
  const currentStream = CONNECTOR_STREAMS.find((s) => s.id === boundStreamId)
  const canTransmit = Boolean(char.read || char.notify || char.indicate)
  const canReceive = Boolean(char.write || char.writeNoResponse)

  return (
    <div className="object-details ble-details">
      <div className="object-details-header">
        <Radio className="object-check-eye" />
        <h2>{char.name}</h2>
        {isSystemBleChar(char) && <span className="conn-system-label">SYS</span>}
        <span className="ble-uuid-chip">{char.uuid}</span>
        <span className="object-details-badge">{char.format}</span>
        {!isSystemBleChar(char) && (
          <button
            type="button"
            className="object-main-delete"
            title="Delete characteristic"
            aria-label={`Delete ${char.name}`}
            onClick={() => w.removeCharacteristic(service.id, char.id)}
          >
            <Trash2 />
          </button>
        )}
      </div>

      <div className="object-details-section">
        <h3>Properties</h3>
        <div className="object-details-toggles">
          <label className="object-details-check">
            <input
              type="checkbox"
              checked={char.read}
              onChange={(e) => {
                const nextRead = e.target.checked
                const willCanTx = nextRead || char.notify || char.indicate
                w.updateCharacteristic(service.id, char.id, {
                  read: nextRead,
                  ...(!willCanTx ? { txBufferSize: 0 } : {}),
                })
              }}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Read</span>
          </label>
          <label className="object-details-check">
            <input
              type="checkbox"
              checked={char.write}
              onChange={(e) => {
                const nextWrite = e.target.checked
                const willCanRx = nextWrite || char.writeNoResponse
                w.updateCharacteristic(service.id, char.id, {
                  write: nextWrite,
                  ...(!willCanRx ? { rxBufferSize: 0 } : {}),
                })
              }}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Write</span>
          </label>
          <label className="object-details-check">
            <input
              type="checkbox"
              checked={char.writeNoResponse}
              onChange={(e) => {
                const nextWnr = e.target.checked
                const willCanRx = char.write || nextWnr
                w.updateCharacteristic(service.id, char.id, {
                  writeNoResponse: nextWnr,
                  ...(!willCanRx ? { rxBufferSize: 0 } : {}),
                })
              }}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Write Without Response (Fast)</span>
          </label>
          <label className="object-details-check">
            <input
              type="checkbox"
              checked={char.notify}
              onChange={(e) => {
                const nextNotify = e.target.checked
                const willCanTx = char.read || nextNotify || char.indicate
                w.updateCharacteristic(service.id, char.id, {
                  notify: nextNotify,
                  ...(!willCanTx ? { txBufferSize: 0 } : {}),
                })
              }}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Notify</span>
          </label>
          <label className="object-details-check">
            <input
              type="checkbox"
              checked={char.indicate}
              onChange={(e) => {
                const nextIndicate = e.target.checked
                const willCanTx = char.read || char.notify || nextIndicate
                w.updateCharacteristic(service.id, char.id, {
                  indicate: nextIndicate,
                  ...(!willCanTx ? { txBufferSize: 0 } : {}),
                })
              }}
            />
            <span className="object-check-box" aria-hidden="true" />
            <span>Indicate</span>
          </label>
        </div>
      </div>

      <div className="object-details-section">
        <h3>Default Value</h3>
        <label>
          Initial Value
          <input
            value={char.initialValue ?? ''}
            placeholder={char.format === 'STR' ? 'Default text' : '0 or hex'}
            onChange={(e) => w.updateCharacteristic(service.id, char.id, { initialValue: e.target.value })}
          />
        </label>
      </div>

      <div className="object-details-section">
        <h3>Relations to connectors</h3>
        <label>
          Connector Stream Binding
          <select
            value={boundStreamId ?? 'none'}
            onChange={(e) => {
              const val = e.target.value === 'none' ? undefined : e.target.value
              w.updateCharacteristic(service.id, char.id, { connectorStream: val })
            }}
          >
            <option value="none">None (Custom Characteristic)</option>
            {CONNECTOR_STREAMS.map((st) => (
              <option key={st.id} value={st.id}>
                {st.alias} · {st.direction === 'in' ? 'IN' : st.direction === 'out' ? 'OUT' : 'IN/OUT'}
              </option>
            ))}
          </select>
        </label>

        {currentStream ? (
          <div className="ble-connector-card">
            <div className="ble-connector-header">
              <span className="ble-connector-title">{currentStream.name}</span>
              <span className={`ble-direction-tag ${currentStream.direction}`}>
                {currentStream.direction === 'in' ? 'IN' : currentStream.direction === 'out' ? 'OUT' : 'IN/OUT'}
              </span>
            </div>
            <p className="ble-connector-desc">{currentStream.description}</p>
            <div className="ble-connector-meta">
              <span><strong>Stream ID:</strong> {currentStream.header}</span>
            </div>
          </div>
        ) : (
          <p className="ble-connector-desc">This characteristic is not bound to a board connector stream.</p>
        )}
      </div>

      {(canTransmit || canReceive) && (
        <div className="object-details-section">
          <h3>Buffer Sizes</h3>
          {canTransmit && (
            <div className="ble-form-row">
              <label>Transmit Buffer</label>
              <input
                type="number"
                min="0"
                step="64"
                className="ble-input-field"
                value={char.txBufferSize ?? 0}
                onChange={(e) =>
                  w.updateCharacteristic(service.id, char.id, {
                    txBufferSize: Math.max(0, parseInt(e.target.value) || 0),
                  })
                }
              />
              <div className="ble-preset-chips">
                {[
                  { label: 'None', val: 0 },
                  { label: '512 B', val: 512 },
                  { label: '1 KB', val: 1024 },
                  { label: '2 KB', val: 2048 },
                ].map((preset) => (
                  <button
                    key={preset.val}
                    type="button"
                    className={`ble-preset-chip ${(char.txBufferSize ?? 0) === preset.val ? 'active' : ''}`}
                    onClick={() => w.updateCharacteristic(service.id, char.id, { txBufferSize: preset.val })}
                  >
                    {preset.label}
                  </button>
                ))}
              </div>
            </div>
          )}

          {canReceive && (
            <div className="ble-form-row">
              <label>Receive Buffer</label>
              <input
                type="number"
                min="0"
                step="64"
                className="ble-input-field"
                value={char.rxBufferSize ?? 0}
                onChange={(e) =>
                  w.updateCharacteristic(service.id, char.id, {
                    rxBufferSize: Math.max(0, parseInt(e.target.value) || 0),
                  })
                }
              />
              <div className="ble-preset-chips">
                {[
                  { label: 'None', val: 0 },
                  { label: '512 B', val: 512 },
                  { label: '1 KB', val: 1024 },
                  { label: '2 KB', val: 2048 },
                ].map((preset) => (
                  <button
                    key={preset.val}
                    type="button"
                    className={`ble-preset-chip ${(char.rxBufferSize ?? 0) === preset.val ? 'active' : ''}`}
                    onClick={() => w.updateCharacteristic(service.id, char.id, { rxBufferSize: preset.val })}
                  >
                    {preset.label}
                  </button>
                ))}
              </div>
            </div>
          )}
        </div>
      )}

      <div className="object-details-section">
        <h3>Description</h3>
        <input
          type="text"
          value={char.description ?? ''}
          placeholder="Optional description"
          onChange={(e) => w.updateCharacteristic(service.id, char.id, { description: e.target.value })}
        />
      </div>

      <div className="object-details-section">
        <h3>Parent Service</h3>
        <button
          className="ble-related-char-button"
          onClick={() => {
            w.select(service.id)
            onJump?.(service.id)
          }}
        >
          <Layers className="object-type-icon folder" />
          <span className="ble-related-name">{service.name}</span>
          <span className="ble-uuid-chip">{service.uuid}</span>
          <ChevronRight />
        </button>
      </div>
    </div>
  )
}
