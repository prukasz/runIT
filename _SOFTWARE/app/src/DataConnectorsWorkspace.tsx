import { useEffect, useMemo, useState, useCallback } from 'react'
import {
  ArrowLeft,
  ArrowLeftRight,
  Cable,
  ChevronRight,
  Network,
  Plus,
  Radio,
  Search,
  Shield,
  Trash2,
  X,
} from 'lucide-react'
import { TreeSlab } from './components/TreeSlab'
import type { BleProfile } from './BleSettingsWorkspace'
import { runitEnumValue, runitStreamCatalog } from './domain/descriptors'
import { parseSettings } from './domain/project'
import type { ConnectorBindingSettings, ConnectorSettings } from './domain/project'
import { boardConnectors, defaultProjectSettings, refreshSettings } from './domain/settings'
import { uartEndpointText } from './domain/upload'

// Editor state = the project's settings section (domain/project).
export type DataConnectorBinding = ConnectorBindingSettings
export type DataConnectorItem = ConnectorSettings

/** Auto-saved on every change, like the object tree; also saved in the project file. */
const STORAGE_KEY = 'runit.connectors'

// The board's connectors, bindings and registry limits (streams.generated.json).
const streamCatalog = runitStreamCatalog()
const board = streamCatalog.board
export const CONNECTOR_LIMITS = board.limits
const PROVIDER_IDS = {
  BLE: runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_BLE'),
  UART: runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_UART'),
} as const
const APP_CONNECTOR_BASE = runitEnumValue('sys_data_connector_id_e', 'SYS_DATA_CONNECTOR_APP_BASE')
const hex = (value: number, digits: number): string => `0x${value.toString(16).padStart(digits, '0').toUpperCase()}`

/** The board's UART endpoints, for the binding form (from its default bindings). */
export const UART_ENDPOINTS = [...new Map(board.bindings.filter((b) => b.providerId === PROVIDER_IDS.UART).map((b) => [b.endpoint, uartEndpointText(b.endpoint, b.endpointSymbol)])).values()]

/** The connectors a board starts with (domain defaults + editor-only fields). */
export const DEFAULT_CONNECTORS: DataConnectorItem[] = boardConnectors()

/** The saved connectors, the system ones refreshed from the descriptors (refreshSettings). */
const loadConnectors = (): DataConnectorItem[] => {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) return [...refreshSettings(parseSettings({ ble: defaultProjectSettings().ble, connectors: JSON.parse(raw) }, 'connectors')).connectors]
  } catch {
    /* Storage unavailable or unreadable: start from the defaults. */
  }
  return DEFAULT_CONNECTORS
}

export interface DataConnectorsWorkspace {
  connectors: DataConnectorItem[]
  selectedKey: string
  selectedConnector: DataConnectorItem | undefined
  select: (key: string) => void
  addConnector: () => void
  updateConnector: (key: string, updates: Partial<DataConnectorItem>) => void
  deleteConnector: (key: string) => void
  addBinding: (key: string, binding: Omit<DataConnectorBinding, 'id'>) => void
  removeBinding: (key: string, bindingId: string) => void
  toggleSuspend: (key: string) => void
  /** Replace every connector (project opened or recovered). */
  load: (connectors: readonly DataConnectorItem[]) => void
}

export function useDataConnectorsWorkspace(onSelect?: (key: string) => void): DataConnectorsWorkspace {
  const [connectors, setConnectors] = useState<DataConnectorItem[]>(loadConnectors)
  const [selectedKey, setSelectedKey] = useState<string>('telemetry')

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(connectors))
    } catch {
      /* The project file keeps them. */
    }
  }, [connectors])

  const load = useCallback((next: readonly DataConnectorItem[]) => setConnectors([...next]), [])

  const select = useCallback((key: string) => {
    setSelectedKey(key)
    onSelect?.(key)
  }, [onSelect])

  const selectedConnector = useMemo(
    () => connectors.find((c) => c.key === selectedKey) || connectors[0],
    [connectors, selectedKey]
  )

  const addConnector = useCallback(() => {
    setConnectors((prev) => {
      // First free user ID (SYS_DATA_CONNECTOR_APP_BASE .. CONFIG_SYS_DATA_CONNECTOR_MAX - 1).
      const taken = new Set(prev.map((c) => c.id))
      let nextId = APP_CONNECTOR_BASE
      while (taken.has(nextId)) nextId++
      if (nextId >= CONNECTOR_LIMITS.connectorsMax) return prev
      // First stream byte no connector uses yet.
      const headers = new Set(prev.map((c) => Number.parseInt(c.header, 16)))
      let header = 1
      while (headers.has(header) && header < 0xff) header++
      const nextHex = hex(header, 2)
      const key = `connector_${nextId}`
      const newConn: DataConnectorItem = {
        id: nextId,
        key,
        name: `user_stream_${nextId}`,
        alias: `Custom Connector ${nextId}`,
        header: nextHex,
        system: false,
        description: 'User-defined logical stream for application-specific packets.',
        direction: 'TX',
        maxPacketLen: CONNECTOR_LIMITS.frameMax,
        isSuspended: false,
        cMacro: `SYS_DATA_CONNECTOR_APP_${nextId}`,
        bindings: [],
      }
      setSelectedKey(key)
      return [...prev, newConn]
    })
  }, [])

  const updateConnector = useCallback((key: string, updates: Partial<DataConnectorItem>) => {
    setConnectors((prev) =>
      prev.map((c) => (c.key === key ? { ...c, ...updates } : c))
    )
  }, [])

  const deleteConnector = useCallback((key: string) => {
    setConnectors((prev) => {
      const target = prev.find((c) => c.key === key)
      if (target?.system) return prev // Protected system connector
      const next = prev.filter((c) => c.key !== key)
      if (selectedKey === key && next.length) {
        setSelectedKey(next[0].key)
      }
      return next
    })
  }, [selectedKey])

  const addBinding = useCallback((key: string, binding: Omit<DataConnectorBinding, 'id'>) => {
    setConnectors((prev) =>
      prev.map((c) => {
        if (c.key !== key) return c
        const newBinding: DataConnectorBinding = {
          id: `b_${Date.now()}_${Math.random().toString(36).substring(2, 6)}`,
          ...binding,
        }
        return { ...c, bindings: [...c.bindings, newBinding] }
      })
    )
  }, [])

  const removeBinding = useCallback((key: string, bindingId: string) => {
    setConnectors((prev) =>
      prev.map((c) => {
        if (c.key !== key) return c
        return { ...c, bindings: c.bindings.filter((b) => b.id !== bindingId) }
      })
    )
  }, [])

  const toggleSuspend = useCallback((key: string) => {
    setConnectors((prev) =>
      prev.map((c) => (c.key === key ? { ...c, isSuspended: !c.isSuspended } : c))
    )
  }, [])

  return {
    connectors,
    selectedKey,
    selectedConnector,
    select,
    addConnector,
    updateConnector,
    deleteConnector,
    addBinding,
    removeBinding,
    toggleSuspend,
    load,
  }
}

export interface DataConnectorsPaletteProps {
  workspace: DataConnectorsWorkspace
  onBackToSettings?: () => void
}

export function DataConnectorsPalette({ workspace, onBackToSettings }: DataConnectorsPaletteProps) {
  const [search, setSearch] = useState('')
  const { connectors, selectedKey, select, addConnector } = workspace

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase()
    if (!q) return connectors
    return connectors.filter(
      (c) =>
        c.name.toLowerCase().includes(q) ||
        c.alias.toLowerCase().includes(q) ||
        c.header.toLowerCase().includes(q) ||
        c.cMacro.toLowerCase().includes(q)
    )
  }, [connectors, search])

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

      <div className="object-tree-search-bar">
        <Search className="search-icon" aria-hidden="true" />
        <input
          type="text"
          placeholder="Search data connectors..."
          aria-label="Search data connectors"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === 'Escape') setSearch('')
          }}
        />
        {search && (
          <button
            type="button"
            className="search-clear-btn"
            title="Clear search"
            onClick={() => setSearch('')}
          >
            <X aria-hidden="true" />
          </button>
        )}
      </div>

      <div className="object-tree-heading">
        <span className="ble-palette-title">Data Connectors</span>
        <button
          title="Add Custom Connector"
          aria-label="Add Custom Connector"
          onClick={addConnector}
        >
          <Plus aria-hidden="true" />
        </button>
      </div>

      <div className="object-tree-scroll" role="navigation" aria-label="Data Connectors list">
        {filtered.map((conn) => {
          const isSelected = selectedKey === conn.key
          return (
            <TreeSlab
              key={conn.key}
              selected={isSelected}
              onClick={() => select(conn.key)}
              icon={
                <span className={`object-type-icon ${conn.system ? 'folder' : 'text'}`} title={conn.system ? 'System Connector' : 'Custom Connector'}>
                  {conn.system ? <Shield aria-hidden="true" /> : <Cable aria-hidden="true" />}
                </span>
              }
              label={conn.alias}
              badges={
                <>
                  {conn.system && <span className="conn-system-label">SYS</span>}
                  <span className="ble-uuid-chip">{conn.header}</span>
                </>
              }
            />
          )
        })}

        {!filtered.length && (
          <p className="object-tree-empty">
            {search ? `No connectors matching "${search}"` : 'No connectors configured.'}
          </p>
        )}
      </div>
    </div>
  )
}

const DEFAULT_BLE_ENDPOINT = hex((board.characteristics.find((c) => c.notify) ?? board.characteristics[0])?.uuid ?? board.service.uuid, 4)
const DEFAULT_UART_ENDPOINT = UART_ENDPOINTS[0] ?? '0'

export interface DataConnectorsEditorProps {
  workspace: DataConnectorsWorkspace
  bleProfile?: BleProfile
}

export function DataConnectorsEditor({ workspace, bleProfile }: DataConnectorsEditorProps) {
  const { selectedConnector, updateConnector, deleteConnector, addBinding, removeBinding, toggleSuspend } = workspace
  const [newProvider, setNewProvider] = useState<'BLE' | 'UART'>('BLE')
  const [newEndpoint, setNewEndpoint] = useState(DEFAULT_BLE_ENDPOINT)
  const [newDirection, setNewDirection] = useState<'TX' | 'RX' | 'TX_RX'>('TX')

  if (!selectedConnector) {
    return (
      <div className="object-editor">
        <p>No connector selected.</p>
      </div>
    )
  }

  const handleAddBinding = () => {
    addBinding(selectedConnector.key, {
      provider: newProvider,
      endpoint: newEndpoint.trim() || (newProvider === 'BLE' ? DEFAULT_BLE_ENDPOINT : DEFAULT_UART_ENDPOINT),
      direction: newDirection,
    })
  }

  return (
    <div className="object-editor ble-editor">
      <div className="ble-general-header">
        <Network aria-hidden="true" />
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <h1 style={{ margin: 0 }}>{selectedConnector.alias}</h1>
            {selectedConnector.system && (
              <span className="conn-system-label">system</span>
            )}
          </div>
          <span style={{ fontSize: '12px', color: 'var(--muted)' }}>
            Stream ID: {selectedConnector.header}
          </span>
        </div>
        {!selectedConnector.system && (
          <button
            type="button"
            className="object-main-delete"
            title="Delete this custom connector"
            onClick={() => deleteConnector(selectedConnector.key)}
          >
            <Trash2 aria-hidden="true" />
          </button>
        )}
      </div>

      <div className="ble-editor-grid">
        {/* Stream Settings Card */}
        <div className="ble-card">
          <div className="card-header-badge">
            <Cable aria-hidden="true" />
            <h3>Stream Settings</h3>
          </div>
          <p className="card-subhead">
            Basic configuration and buffer size for this data stream.
          </p>

          <div className="ble-two-col-grid">
            <div className="ble-form-row">
              <label>Stream Name</label>
              <input
                type="text"
                className="ble-input-field"
                value={selectedConnector.alias}
                onChange={(e) => updateConnector(selectedConnector.key, { alias: e.target.value })}
              />
            </div>
            <div className="ble-form-row">
              <label>Stream ID</label>
              <input
                type="text"
                className="ble-input-field"
                value={selectedConnector.header}
                disabled={selectedConnector.system}
                placeholder="0xNN"
                maxLength={6}
                onChange={(e) => updateConnector(selectedConnector.key, { header: e.target.value })}
              />
            </div>
          </div>

          <div className="ble-two-col-grid">
            <div className="ble-form-row">
              <label>Buffer Size</label>
              <input
                type="number"
                className="ble-input-field"
                value={selectedConnector.maxPacketLen}
                min={2}
                max={CONNECTOR_LIMITS.frameMax}
                step={64}
                onChange={(e) => updateConnector(selectedConnector.key, { maxPacketLen: Math.min(CONNECTOR_LIMITS.frameMax, Math.max(2, parseInt(e.target.value) || CONNECTOR_LIMITS.frameMax)) })}
              />
              <div className="ble-preset-chips">
                {[
                  { label: '256 B', val: 256 },
                  { label: '512 B', val: 512 },
                  { label: '1 KB', val: 1024 },
                  { label: '2 KB', val: 2048 },
                ].filter((preset) => preset.val <= CONNECTOR_LIMITS.frameMax).map((preset) => (
                  <button
                    key={preset.val}
                    type="button"
                    className={`ble-preset-chip ${selectedConnector.maxPacketLen === preset.val ? 'active' : ''}`}
                    onClick={() => updateConnector(selectedConnector.key, { maxPacketLen: preset.val })}
                  >
                    {preset.label}
                  </button>
                ))}
              </div>
            </div>

            <div className="ble-form-row">
              <label>Useful Payload</label>
              <input
                type="text"
                className="ble-input-field"
                value={`${Math.max(0, selectedConnector.maxPacketLen - 1)} B`}
                disabled
                readOnly
              />
            </div>
          </div>

          <div className="ble-form-row">
            <label>Description</label>
            <input
              type="text"
              className="ble-input-field"
              value={selectedConnector.description}
              placeholder="Optional description"
              onChange={(e) => updateConnector(selectedConnector.key, { description: e.target.value })}
            />
          </div>

          {/* Pause / Resume Option */}
          <div className="ble-ticks-section">
            <label className="ble-tick-label">
              <input
                type="checkbox"
                checked={selectedConnector.isSuspended}
                onChange={() => toggleSuspend(selectedConnector.key)}
              />
              <span className="object-check-box" />
              <div>
                <span className="tick-title">Pause Stream</span>
                <span className="tick-desc">
                  Temporarily pause data traffic on this stream.
                </span>
              </div>
            </label>
          </div>
        </div>

        {/* Connected Transports Card */}
        <div className="ble-card">
          <div className="card-header-badge">
            <ArrowLeftRight aria-hidden="true" />
            <h3>Connected Transports</h3>
          </div>
          <p className="card-subhead">
            Physical communication channels connected to this stream.
          </p>

          <div className="conn-bindings-table">
            <div className="conn-bindings-head">
              <span>Transport</span>
              <span>Endpoint</span>
              <span>Direction</span>
              <span>Action</span>
            </div>
            {selectedConnector.bindings.map((b) => (
              <div key={b.id} className="conn-binding-row">
                <span className={`conn-prov-badge ${b.provider.toLowerCase()}`}>
                  {b.provider === 'BLE' ? <Radio aria-hidden="true" /> : <Cable aria-hidden="true" />}
                  {b.provider === 'BLE' ? 'Bluetooth' : 'Serial'}
                </span>
                <span className="conn-endpoint-val">{b.endpoint}</span>
                <span className={`ble-direction-tag ${b.direction === 'TX_RX' ? 'inout' : b.direction === 'RX' ? 'in' : 'out'}`}>
                  {b.direction === 'TX_RX' ? 'In / Out' : b.direction === 'TX' ? 'Outbound' : 'Inbound'}
                </span>
                <span>
                  <button
                    type="button"
                    className="conn-binding-remove-btn"
                    title="Remove binding"
                    onClick={() => removeBinding(selectedConnector.key, b.id)}
                  >
                    <Trash2 aria-hidden="true" />
                  </button>
                </span>
              </div>
            ))}
            {!selectedConnector.bindings.length && (
              <p className="conn-bindings-empty">No transports currently connected to this stream.</p>
            )}
          </div>

          {/* Add Binding Bar */}
          <div className="conn-add-binding-bar">
            <h4>Connect Transport</h4>
            <div className="conn-add-binding-controls">
              <select
                className="ble-select-field"
                value={newProvider}
                onChange={(e) => {
                  const val = e.target.value as 'BLE' | 'UART'
                  setNewProvider(val)
                  setNewEndpoint(val === 'BLE' ? DEFAULT_BLE_ENDPOINT : DEFAULT_UART_ENDPOINT)
                }}
              >
                <option value="BLE">Bluetooth LE</option>
                <option value="UART">Serial Console (UART)</option>
              </select>

              {newProvider === 'BLE' ? (
                <select
                  className="ble-select-field"
                  value={newEndpoint}
                  onChange={(e) => setNewEndpoint(e.target.value)}
                >
                  {board.characteristics.map((c) => (
                    <option key={c.symbol} value={hex(c.uuid, 4)}>
                      {hex(c.uuid, 4)} ({c.name})
                    </option>
                  ))}
                  {bleProfile?.services.flatMap((s) => s.characteristics)
                    .filter((c) => !board.characteristics.some((own) => hex(own.uuid, 4) === c.uuid.toUpperCase().replace(/^0X/, '0x')))
                    .map((c) => (
                      <option key={c.id} value={c.uuid}>
                        {c.uuid} ({c.name})
                      </option>
                    ))}
                </select>
              ) : (
                <input
                  type="text"
                  className="ble-input-field"
                  value={newEndpoint}
                  onChange={(e) => setNewEndpoint(e.target.value)}
                  placeholder={DEFAULT_UART_ENDPOINT}
                />
              )}

              <select
                className="ble-select-field"
                value={newDirection}
                onChange={(e) => setNewDirection(e.target.value as 'TX' | 'RX' | 'TX_RX')}
              >
                <option value="TX">Outbound (Send)</option>
                <option value="RX">Inbound (Receive)</option>
                <option value="TX_RX">Bidirectional (In / Out)</option>
              </select>

              <button
                type="button"
                className="conn-add-btn"
                onClick={handleAddBinding}
              >
                <Plus aria-hidden="true" />
                <span>Add</span>
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

export interface DataConnectorDetailsProps {
  workspace: DataConnectorsWorkspace
  onJumpToBle?: () => void
}

export function DataConnectorDetails({ workspace, onJumpToBle }: DataConnectorDetailsProps) {
  const { selectedConnector } = workspace

  if (!selectedConnector) {
    return (
      <div className="object-details">
        <p>No connector selected.</p>
      </div>
    )
  }

  return (
    <div className="object-details ble-details">
      <div className="object-details-header">
        <span className="object-type-icon folder">
          <Network aria-hidden="true" />
        </span>
        <h2>{selectedConnector.alias}</h2>
        <span className="object-details-badge">{selectedConnector.header}</span>
      </div>

      <div className="object-details-section">
        <h3>Stream Details</h3>
        <p><strong>Name:</strong> {selectedConnector.alias}</p>
        <p><strong>Stream ID:</strong> {selectedConnector.header}</p>
        <p><strong>Type:</strong> {selectedConnector.system ? 'System' : 'Custom'}</p>
        <p><strong>Buffer Size:</strong> {selectedConnector.maxPacketLen} B</p>
        <p><strong>Status:</strong> {selectedConnector.isSuspended ? 'Paused' : 'Active'}</p>
        {selectedConnector.description && (
          <p style={{ marginTop: '8px', fontSize: '12px', color: 'var(--muted)', lineHeight: '1.4' }}>
            {selectedConnector.description}
          </p>
        )}
      </div>

      <div className="object-details-section">
        <h3>Connected Transports ({selectedConnector.bindings.length})</h3>
        <div className="ble-connector-summary-list">
          {selectedConnector.bindings.map((b) => (
            <div key={b.id} className="ble-connector-row">
              <div className="ble-connector-char-info">
                <span className="ble-connector-char-name">
                  {b.provider === 'BLE' ? 'Bluetooth' : 'Serial'} → {b.endpoint}
                </span>
                <span className="ble-connector-target">
                  Direction: {b.direction === 'TX_RX' ? 'In / Out' : b.direction === 'TX' ? 'Outbound' : 'Inbound'}
                </span>
              </div>
              <span className={`ble-direction-tag ${b.direction === 'TX_RX' ? 'inout' : b.direction === 'RX' ? 'in' : 'out'}`}>
                {b.direction === 'TX_RX' ? 'Both' : b.direction}
              </span>
            </div>
          ))}
          {!selectedConnector.bindings.length && (
            <p style={{ fontStyle: 'italic', fontSize: '11px', color: 'var(--muted)' }}>
              No transports connected.
            </p>
          )}
        </div>
      </div>

      {onJumpToBle && (
        <div className="object-details-section">
          <h3>Bluetooth Profile</h3>
          <button
            type="button"
            className="ble-subtab-button"
            onClick={onJumpToBle}
            style={{ width: '100%' }}
          >
            <Radio aria-hidden="true" />
            <span>Open BLE Profile Settings</span>
            <ChevronRight aria-hidden="true" style={{ marginLeft: 'auto' }} />
          </button>
        </div>
      )}
    </div>
  )
}
