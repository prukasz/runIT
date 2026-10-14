import { useMemo, useState } from 'react'
import { ArrowLeft, Cable, ChevronRight, Network, Plus, Radio, RotateCcw, Shield, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Card, CardStack } from '../components/Card'
import { ChoiceField } from '../components/ChoiceField'
import { FormGrid, FormRow, TextField } from '../components/FormField'
import { PaletteSearch } from '../components/PaletteSearch'
import { PanelHeader } from '../components/PanelHeader'
import { Switch } from '../components/Switch/Switch'
import { TreeSlab } from '../components/TreeSlab'
import { boardSetup, connectorModified, hex, removedBindings, uartEndpointText } from '../domain/settings'
import type { BleProfile, ConnectorSettings, Provider } from '../domain/settings'
import type { ConnectorSettingsState } from './useConnectorSettings'

/*
 * The data connectors screens: the palette (the board's four system streams and the user's own), the editor (stream settings and
 * the transports it sends on) and the details. A connector is a logical outbound stream with a stream byte; it is bound to
 * transports (Bluetooth at a characteristic, serial at the console), one binding per transport. Streams only go out: commands
 * and their answers use the router links.
 */

const providerName = (provider: Provider): string => (provider === 'BLE' ? 'Bluetooth' : 'Serial')

function ProviderBadge({ provider }: { provider: Provider }) {
  return (
    <span className={`conn-prov-badge ${provider.toLowerCase()}`}>
      {provider === 'BLE' ? <Radio aria-hidden="true" /> : <Cable aria-hidden="true" />}
      {providerName(provider)}
    </span>
  )
}

export function ConnectorSettingsPalette({ workspace, onBack }: { workspace: ConnectorSettingsState; onBack: () => void }) {
  const [search, setSearch] = useState('')
  const { connectors, selectedKey, select, addConnector, restoreAll, modified } = workspace
  const { limits, appBase } = boardSetup()
  const full = connectors.length >= limits.connectorsMax

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase()
    return q ? connectors.filter((connector) => connector.name.toLowerCase().includes(q) || connector.alias.toLowerCase().includes(q) || connector.stream.toLowerCase().includes(q)) : connectors
  }, [connectors, search])

  return (
    <div className="object-tree-palette ble-palette">
      <div className="ble-palette-back-bar">
        <button type="button" className="settings-back-btn" onClick={onBack} title="Back to the project">
          <ArrowLeft aria-hidden="true" />
          <span>Project</span>
        </button>
      </div>

      <PaletteSearch value={search} onChange={setSearch} placeholder="Search data connectors..." label="Search data connectors" />

      <div className="object-tree-heading">
        <span className="ble-palette-title">Data Connectors</span>
        <button
          title={modified ? 'Restore the system streams as the board has them: transports and pauses (your own streams stay)' : 'The system streams match the board'}
          aria-label="Restore the system connectors of the board"
          disabled={!modified}
          onClick={restoreAll}
        >
          <RotateCcw aria-hidden="true" />
        </button>
        <button title={full ? `The board holds ${limits.connectorsMax} connectors (${appBase} system and user)` : 'Add Custom Connector'} aria-label="Add Custom Connector" disabled={full} onClick={addConnector}>
          <Plus aria-hidden="true" />
        </button>
      </div>

      <div className="object-tree-scroll" role="navigation" aria-label="Data Connectors list">
        {filtered.map((connector) => (
          <TreeSlab
            key={connector.key}
            selected={selectedKey === connector.key}
            onClick={() => select(connector.key)}
            icon={<span className={`object-type-icon ${connector.system ? 'folder' : 'text'}`} title={connector.system ? 'System Connector' : 'Custom Connector'}>{connector.system ? <Shield aria-hidden="true" /> : <Cable aria-hidden="true" />}</span>}
            label={connector.alias}
            badges={<>{connector.system && <Badge tone="system">SYS</Badge>}<Badge push>{connector.stream}</Badge></>}
          />
        ))}
        {!filtered.length && <p className="object-tree-empty">{search ? `No connectors matching "${search}"` : 'No connectors configured.'}</p>}
      </div>
    </div>
  )
}

const SETUP_LIMITS = () => boardSetup().limits

export function ConnectorSettingsEditor({ workspace, bleProfile }: { workspace: ConnectorSettingsState; bleProfile: BleProfile }) {
  const { selectedConnector: connector, updateConnector, deleteConnector, addBinding, removeBinding, toggleSuspend, restoreConnector: restoreStream, restoreBinding: restoreTransport } = workspace
  const setup = boardSetup()
  const limits = SETUP_LIMITS()
  const bleChoices = useMemo(() => {
    const own = setup.characteristics.filter((char) => char.notify || char.indicate).map((char) => ({ value: hex(char.uuid, 4), label: `${hex(char.uuid, 4)} (${char.name})` }))
    const mine = bleProfile.services.flatMap((service) => service.characteristics).filter((char) => !char.system && (char.notify || char.indicate)).map((char) => ({ value: char.uuid, label: `${char.uuid} (${char.name})` }))
    return [...own, ...mine]
  }, [setup, bleProfile])
  const defaultBle = bleChoices[0]?.value ?? hex(setup.service.uuid, 4)
  const defaultUart = uartEndpointText()
  const [provider, setProvider] = useState<Provider>('BLE')
  const [endpoint, setEndpoint] = useState(defaultBle)

  if (!connector) return <div className="object-editor"><p>No connector selected.</p></div>

  const bindingsFull = connector.bindings.length >= limits.bindingsPerConnector && !connector.bindings.some((binding) => binding.provider === provider)

  return (
    <div className="object-editor ble-editor">
      <div className="ble-general-header">
        <Network aria-hidden="true" />
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <h1 style={{ margin: 0 }}>{connector.alias}</h1>
            {connector.system && <Badge tone="system">system</Badge>}
          </div>
          <span style={{ fontSize: '12px', color: 'var(--muted)' }}>Stream byte: {connector.stream} · ID {connector.id}</span>
        </div>
        {connector.system && connectorModified(connector) && (
          <button type="button" className="conn-add-btn" title="Put this stream back as the board has it: transports and pause" onClick={() => restoreStream(connector.key)}>
            <RotateCcw aria-hidden="true" />
            <span>Restore defaults</span>
          </button>
        )}
        {!connector.system && (
          <button type="button" className="object-main-delete" title="Delete this custom connector" aria-label="Delete this custom connector" onClick={() => deleteConnector(connector.key)}>
            <Trash2 aria-hidden="true" />
          </button>
        )}
      </div>

      <CardStack>
        <Card icon={<Cable aria-hidden="true" />} title="Stream Settings" subhead="The name the board keeps and the byte every frame of this stream starts with.">
          <FormGrid>
            <FormRow label="Stream Name" hint={connector.system ? 'Fixed by the firmware.' : `Up to ${limits.nameMax} characters.`}>
              <TextField
                type="text"
                className="form-field-panel"
                value={connector.alias}
                maxLength={limits.nameMax}
                disabled={connector.system}
                onChange={(event) => updateConnector(connector.key, { alias: event.target.value, name: event.target.value })}
              />
            </FormRow>
            <FormRow label="Stream Byte" hint={connector.system ? 'Fixed by the firmware.' : 'One byte, not one the firmware uses (telemetry, logs, errors, response, events).'}>
              <TextField type="text" className="form-field-panel" value={connector.stream} disabled={connector.system} placeholder="0xNN" maxLength={4} onChange={(event) => updateConnector(connector.key, { stream: event.target.value })} />
            </FormRow>
          </FormGrid>

          <FormGrid>
            <FormRow label="Largest Frame" hint="One cap for every connector (stream byte included).">
              <TextField type="text" className="form-field-panel" value={`${limits.frameMax} B`} disabled readOnly />
            </FormRow>
            <FormRow label="Useful Payload">
              <TextField type="text" className="form-field-panel" value={`${Math.max(0, limits.frameMax - 1)} B`} disabled readOnly />
            </FormRow>
          </FormGrid>

          <FormRow label="Description">
            <TextField type="text" className="form-field-panel" value={connector.description} placeholder="Optional description" disabled={connector.system} onChange={(event) => updateConnector(connector.key, { description: event.target.value })} />
          </FormRow>

          <Switch label="Pause Stream: frames are dropped while it is paused" checked={connector.isSuspended} onChange={() => toggleSuspend(connector.key)} />
        </Card>

        <Card icon={<Radio aria-hidden="true" />} title="Connected Transports" subhead={`Where this stream is sent: one binding per transport, up to ${limits.bindingsPerConnector}. Binding a transport again re-addresses it.`}>
          <div className="conn-bindings-table">
            <div className="conn-bindings-head">
              <span>Transport</span>
              <span>Endpoint</span>
              <span>Action</span>
            </div>
            {connector.bindings.map((binding) => (
              <div key={binding.id} className="conn-binding-row">
                <ProviderBadge provider={binding.provider} />
                <span className="conn-endpoint-val">{binding.endpoint}</span>
                <span>
                  <button type="button" className="conn-binding-remove-btn" title="Remove binding" aria-label={`Remove ${providerName(binding.provider)} ${binding.endpoint}`} onClick={() => removeBinding(connector.key, binding.id)}>
                    <Trash2 aria-hidden="true" />
                  </button>
                </span>
              </div>
            ))}
            {!connector.bindings.length && <p className="conn-bindings-empty">No transports currently connected to this stream. Frames are dropped.</p>}
            {removedBindings(connector).map((binding) => (
              <div key={`removed-${binding.id}`} className="conn-binding-row is-removed" title="The board has this transport by default; it was removed here">
                <ProviderBadge provider={binding.provider} />
                <span className="conn-endpoint-val">{binding.endpoint}</span>
                <Badge tone="system" caps>removed</Badge>
                <span>
                  <button type="button" className="conn-binding-remove-btn" title="Put this transport back" aria-label={`Restore ${providerName(binding.provider)} ${binding.endpoint}`} onClick={() => restoreTransport(connector.key, binding.id)}>
                    <RotateCcw aria-hidden="true" />
                  </button>
                </span>
              </div>
            ))}
          </div>

          <div className="conn-add-binding-bar">
            <h4>Connect Transport</h4>
            <div className="conn-add-binding-controls">
              <ChoiceField<Provider>
                selectClassName="form-field-panel"
                value={provider}
                choices={[{ value: 'BLE', label: 'Bluetooth LE' }, { value: 'UART', label: 'Serial Console (UART)' }]}
                onChange={(next) => {
                  if (!next) return
                  setProvider(next)
                  setEndpoint(next === 'BLE' ? defaultBle : defaultUart)
                }}
              />
              {provider === 'BLE' ? (
                <ChoiceField<string> selectClassName="form-field-panel" value={endpoint} choices={bleChoices} onChange={(next) => next !== undefined && setEndpoint(next)} />
              ) : (
                <TextField type="text" className="form-field-panel" value={endpoint} onChange={(event) => setEndpoint(event.target.value)} placeholder={defaultUart} />
              )}
              <button
                type="button"
                className="conn-add-btn"
                disabled={bindingsFull}
                title={bindingsFull ? `A connector holds ${limits.bindingsPerConnector} bindings` : undefined}
                onClick={() => addBinding(connector.key, { provider, endpoint: endpoint.trim() || (provider === 'BLE' ? defaultBle : defaultUart) })}
              >
                <Plus aria-hidden="true" />
                <span>Add</span>
              </button>
            </div>
          </div>
        </Card>
      </CardStack>
    </div>
  )
}

export function ConnectorDetails({ workspace, onJumpToBle }: { workspace: ConnectorSettingsState; onJumpToBle?: () => void }) {
  const connector: ConnectorSettings | undefined = workspace.selectedConnector
  if (!connector) return <div className="object-details"><p>No connector selected.</p></div>

  return (
    <div className="object-details ble-details">
      <PanelHeader icon={<span className="object-type-icon folder"><Network aria-hidden="true" /></span>} title={connector.alias}>
        <Badge tone="accent" size="detail">{connector.stream}</Badge>
      </PanelHeader>

      <div className="object-details-section">
        <h3>Stream Details</h3>
        <p><strong>Name:</strong> {connector.alias}</p>
        <p><strong>Stream byte:</strong> {connector.stream}</p>
        <p><strong>ID:</strong> {connector.id}</p>
        <p><strong>Type:</strong> {connector.system ? 'System' : 'Custom'}</p>
        <p><strong>Status:</strong> {connector.isSuspended ? 'Paused' : 'Active'}</p>
        {connector.description && <p style={{ marginTop: '8px', fontSize: '12px', color: 'var(--muted)', lineHeight: '1.4' }}>{connector.description}</p>}
      </div>

      <div className="object-details-section">
        <h3>Connected Transports ({connector.bindings.length})</h3>
        <div className="ble-connector-summary-list">
          {connector.bindings.map((binding) => (
            <div key={binding.id} className="ble-connector-row">
              <div className="ble-connector-char-info">
                <span className="ble-connector-char-name">{providerName(binding.provider)} → {binding.endpoint}</span>
              </div>
            </div>
          ))}
          {!connector.bindings.length && <p style={{ fontStyle: 'italic', fontSize: '11px', color: 'var(--muted)' }}>No transports connected.</p>}
        </div>
      </div>

      {onJumpToBle && (
        <div className="object-details-section">
          <h3>Bluetooth Profile</h3>
          <button type="button" className="ble-subtab-button" onClick={onJumpToBle} style={{ width: '100%' }}>
            <Radio aria-hidden="true" />
            <span>Open BLE Profile Settings</span>
            <ChevronRight aria-hidden="true" style={{ marginLeft: 'auto' }} />
          </button>
        </div>
      )}
    </div>
  )
}
