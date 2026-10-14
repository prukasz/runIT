import { useState } from 'react'
import { ArrowLeft, ChevronDown, ChevronRight, FolderPlus, Layers, Network, Plus, Radio, Sliders, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Card, CardStack } from '../components/Card'
import { ChoiceField } from '../components/ChoiceField'
import { EditableField } from '../components/EditableField'
import { FormGrid, FormRow, NumberField, TextField } from '../components/FormField'
import { PaletteSearch } from '../components/PaletteSearch'
import { PanelHeader } from '../components/PanelHeader'
import { Switch } from '../components/Switch/Switch'
import { TreeSlab } from '../components/TreeSlab'
import { connectorsOnCharacteristic, isBoardCharacteristic, isBoardService } from '../domain/settings'
import type { BleCharacteristicSettings, BleGeneralSettings, BleServiceSettings, ConnectorSettings } from '../domain/settings'
import { GENERAL_ID, normalizeUuid16, padUuid16 } from './useBleSettings'
import type { BleSettings } from './useBleSettings'

/*
 * The GATT profile screens: the palette (services and characteristics, search, add / delete), the editor in the middle (the BLE
 * general page, or every service with its characteristics) and the details on the right (properties, buffers, the connectors that
 * send on a characteristic). The board's runIT service is locked: shown, never edited.
 */

const normalize = (text: string): string => text.replace(/\s+/g, '').toLowerCase()

const matches = (text: string, query: string): boolean => {
  const q = normalize(query)
  return !!q && normalize(text).includes(q)
}

/** The properties of a characteristic as short badges (W write, N notify, I indicate, F framed). */
function PropBadges({ char }: { char: BleCharacteristicSettings }) {
  return (
    <div className="ble-prop-badges">
      {char.write && <Badge tone="warning" size="compact" title="Write">W</Badge>}
      {char.notify && <Badge tone="success" size="compact" title="Notify">N</Badge>}
      {char.indicate && <Badge tone="success" size="compact" title="Indicate">I</Badge>}
      {char.framed && <Badge tone="info" size="compact" title="Framed: frames longer than one packet">F</Badge>}
    </div>
  )
}

export function BleSettingsPalette({ workspace: w, onBack }: { workspace: BleSettings; onBack: () => void }) {
  const [query, setQuery] = useState('')
  const searching = !!query.trim()

  const charMatch = (char: BleCharacteristicSettings) => searching && (matches(char.name, query) || matches(char.uuid, query))
  const serviceMatch = (service: BleServiceSettings) => searching && (matches(service.name, query) || matches(service.uuid, query))
  const hasMatchingChars = (service: BleServiceSettings) => searching && service.characteristics.some(charMatch)
  const serviceVisible = (service: BleServiceSettings) => !searching || serviceMatch(service) || hasMatchingChars(service)
  const charVisible = (service: BleServiceSettings, char: BleCharacteristicSettings) => !searching || serviceMatch(service) || charMatch(char)

  const item = w.selectedItem
  const canDeleteSelected = !!item && ((item.kind === 'characteristic' && !isBoardCharacteristic(item.characteristic)) || (item.kind === 'service' && !isBoardService(item.service)))

  return (
    <div className="object-tree-palette ble-palette">
      <div className="ble-palette-back-bar">
        <button type="button" className="settings-back-btn" onClick={onBack} title="Back to the project">
          <ArrowLeft aria-hidden="true" />
          <span>Project</span>
        </button>
      </div>

      <div className="ble-palette-general-top">
        <button className={`ble-subtab-button ${w.selectedId === GENERAL_ID ? 'selected' : ''}`} onClick={() => w.select(GENERAL_ID)} title="Open BLE General Settings">
          <Sliders className="ble-subtab-icon" aria-hidden="true" />
          <span className="ble-subtab-title">BLE General</span>
        </button>
      </div>

      <PaletteSearch value={query} onChange={setQuery} placeholder="Search BLE services & chars..." label="Search BLE services & chars" />

      <div className="object-tree-heading">
        <span className="ble-palette-title">GATT Profile</span>
        <button title="Add Service" aria-label="Add Service" onClick={w.addService}><FolderPlus aria-hidden="true" /></button>
        <button title="Add Characteristic to the selected Service" aria-label="Add Characteristic to the selected Service" onClick={() => w.addCharacteristic()}><Plus aria-hidden="true" /></button>
        {canDeleteSelected && (
          <button
            type="button"
            className="ble-delete-heading-btn"
            title={item?.kind === 'characteristic' ? 'Delete Characteristic (Del)' : 'Delete Service (Del)'}
            aria-label="Delete selected item"
            onClick={() => {
              if (item?.kind === 'characteristic') w.removeCharacteristic(item.service.id, item.characteristic.id)
              else if (item?.kind === 'service') w.removeService(item.service.id)
            }}
          >
            <Trash2 aria-hidden="true" />
          </button>
        )}
      </div>

      <div className="object-tree-scroll" role="navigation" aria-label="BLE GATT Services and Characteristics">
        {w.profile.services.filter(serviceVisible).map((service) => {
          const collapsed = w.collapsed.has(service.id) && !hasMatchingChars(service)
          const system = isBoardService(service)
          return (
            <div key={service.id} className="ble-service-group">
              <TreeSlab
                isFolder
                twoRowOnNarrow
                disclosurePosition="left"
                selected={w.selectedId === service.id}
                isMatch={serviceMatch(service)}
                onClick={() => w.select(service.id)}
                onDoubleClick={() => w.toggleCollapsed(service.id)}
                icon={<span className="object-type-icon folder" title="BLE GATT Service"><Layers aria-hidden="true" /></span>}
                label={service.name}
                badges={<>{system && <Badge tone="system">SYS</Badge>}<Badge push>{service.uuid}</Badge></>}
                disclosure={
                  <button type="button" className="tree-slab-disclosure" aria-label={`${collapsed ? 'Expand' : 'Collapse'} ${service.name}`} onClick={(event) => { event.stopPropagation(); w.toggleCollapsed(service.id) }}>
                    {collapsed ? <ChevronRight /> : <ChevronDown />}
                  </button>
                }
                actions={!system && (
                  <button type="button" className="tree-slab-action ble-tree-delete-btn" title="Delete Service (Del)" aria-label={`Delete ${service.name}`} onClick={(event) => { event.stopPropagation(); w.removeService(service.id) }}>
                    <Trash2 aria-hidden="true" />
                  </button>
                )}
              />
              {!collapsed && (
                <div className="object-tree-children depth-1">
                  {service.characteristics.filter((char) => charVisible(service, char)).map((char) => (
                    <TreeSlab
                      key={char.id}
                      selected={w.selectedId === char.id}
                      twoRowOnNarrow
                      isMatch={charMatch(char)}
                      onClick={() => w.select(char.id)}
                      icon={<span className="object-type-icon text" title="BLE Characteristic"><Radio aria-hidden="true" /></span>}
                      label={char.name}
                      badges={<>{isBoardCharacteristic(char) && <Badge tone="system">SYS</Badge>}<Badge push>{char.uuid}</Badge></>}
                      actions={!isBoardCharacteristic(char) && (
                        <button type="button" className="tree-slab-action ble-tree-delete-btn" title="Delete Characteristic (Del)" aria-label={`Delete ${char.name}`} onClick={(event) => { event.stopPropagation(); w.removeCharacteristic(service.id, char.id) }}>
                          <Trash2 aria-hidden="true" />
                        </button>
                      )}
                    />
                  ))}
                  {!service.characteristics.some((char) => charVisible(service, char)) && <div className="ble-empty-chars">No characteristics. Click + in the heading to add.</div>}
                </div>
              )}
            </div>
          )
        })}
        {!w.profile.services.some(serviceVisible) && <p className="object-tree-empty">{searching ? `No BLE services matching "${query}"` : 'No BLE services configured yet.'}</p>}
      </div>
    </div>
  )
}

function GeneralEditor({ workspace: w }: { workspace: BleSettings }) {
  const gen = w.profile.general
  return (
    <div className="object-editor ble-editor">
      <div className="ble-general-header">
        <Sliders aria-hidden="true" />
        <h1>BLE General Settings</h1>
      </div>
      <p className="program-muted">Kept in the project for reference. The firmware sets these at build time (Kconfig); the `ble` router has no command for them, so they are not sent to the board.</p>
      <CardStack>
        <Card title="Device Identity & Advertising">
          <FormRow label="Broadcast Device Name">
            <TextField type="text" className="form-field-panel" value={gen.deviceName} onChange={(event) => w.updateGeneral({ deviceName: event.target.value })} placeholder="runit" />
          </FormRow>
          <FormGrid>
            <FormRow label="Advertising Interval (ms)">
              <NumberField integer min={20} max={10240} className="form-field-panel" value={gen.advIntervalMs} onCommit={(value) => w.updateGeneral({ advIntervalMs: value })} />
            </FormRow>
            <FormRow label="Fast Advertising Timeout (s)">
              <NumberField integer min={0} max={3600} className="form-field-panel" value={gen.advFastTimeoutSec} onCommit={(value) => w.updateGeneral({ advFastTimeoutSec: value })} />
            </FormRow>
          </FormGrid>
        </Card>

        <Card title="Connection Parameters & RF">
          <FormGrid>
            <FormRow label="Min Connection Interval (ms)">
              <NumberField min={7.5} max={4000} step={1.25} className="form-field-panel" value={gen.minConnIntervalMs} onCommit={(value) => w.updateGeneral({ minConnIntervalMs: value })} />
            </FormRow>
            <FormRow label="Max Connection Interval (ms)">
              <NumberField min={7.5} max={4000} step={1.25} className="form-field-panel" value={gen.maxConnIntervalMs} onCommit={(value) => w.updateGeneral({ maxConnIntervalMs: value })} />
            </FormRow>
            <FormRow label="Supervision Timeout (ms)">
              <NumberField integer min={100} max={32000} className="form-field-panel" value={gen.supervisionTimeoutMs} onCommit={(value) => w.updateGeneral({ supervisionTimeoutMs: value })} />
            </FormRow>
            <FormRow label="Preferred MTU Size (bytes)" hint="Range 23 – 517 B. The board offers its preferred MTU after a client connects.">
              <NumberField integer min={23} max={517} className="form-field-panel" value={gen.mtuSize} onCommit={(value) => w.updateGeneral({ mtuSize: value })} />
            </FormRow>
          </FormGrid>
        </Card>

        <Card icon={<Layers aria-hidden="true" />} title="Frames & Memory" subhead="The runIT characteristics are framed: a frame up to the router's limit goes out in packets of MTU − 4 bytes, so a small MTU only means more packets.">
          <FormGrid>
            <div className="conn-buffer-stat-box">
              <span className="stat-title">Maximum MTU</span>
              <span className="stat-val">{gen.mtuSize} B</span>
              <span className="stat-hint">Negotiated connection packet size limit.</span>
            </div>
            <div className="conn-buffer-stat-box">
              <span className="stat-title">Payload per notification</span>
              <span className="stat-val">{Math.max(20, gen.mtuSize - 3)} B</span>
              <span className="stat-hint">An unframed characteristic sends one item per packet.</span>
            </div>
            <div className="conn-buffer-stat-box">
              <span className="stat-title">Packet of a framed frame</span>
              <span className="stat-val">{Math.max(19, gen.mtuSize - 4)} B</span>
              <span className="stat-hint">After the one-byte fragment header.</span>
            </div>
            <div className="conn-buffer-stat-box">
              <span className="stat-title">Largest frame</span>
              <span className="stat-val">{w.frameMax} B</span>
              <span className="stat-hint">Router and connector frame cap (stream byte included).</span>
            </div>
          </FormGrid>
        </Card>

        <Card title="Security & PIN / Passkey Management">
          <FormRow label="Pairing Security Level">
            <ChoiceField<BleGeneralSettings['securityMode']>
              selectClassName="form-field-panel"
              value={gen.securityMode}
              onChange={(next) => next && w.updateGeneral({ securityMode: next })}
              choices={[{ value: 'just_works', label: 'No Security (Just Works)' }, { value: 'passkey', label: 'Static 6-Digit PIN / Passkey' }, { value: 'mitm', label: 'Encrypted MITM (Man-In-The-Middle Protection)' }]}
            />
          </FormRow>
          {gen.securityMode !== 'just_works' && (
            <FormRow label="Static 6-Digit Passkey PIN">
              <TextField type="text" maxLength={6} className="form-field-panel" value={gen.passkeyPin} onChange={(event) => w.updateGeneral({ passkeyPin: event.target.value.replace(/\D/g, '') })} placeholder="123456" />
            </FormRow>
          )}
        </Card>
      </CardStack>
    </div>
  )
}

export function BleSettingsEditor({ workspace: w }: { workspace: BleSettings }) {
  if (w.selectedId === GENERAL_ID) return <GeneralEditor workspace={w} />

  return (
    <div className="object-editor ble-editor">
      <div className="object-main-list">
        {w.profile.services.map((service) => {
          const collapsed = w.collapsed.has(service.id)
          const system = isBoardService(service)
          return (
            <div key={service.id} className="object-main-entry depth-0">
              <div className={`object-main-card folder ${w.selectedId === service.id ? 'selected' : ''}`} onClick={() => w.select(service.id)}>
                <div className="object-main-row object-main-top">
                  <button type="button" className="object-main-disclosure" title={collapsed ? 'Expand service' : 'Collapse service'} aria-label={`${collapsed ? 'Expand' : 'Collapse'} ${service.name}`} onClick={(event) => { event.stopPropagation(); w.toggleCollapsed(service.id) }}>
                    {collapsed ? <ChevronRight /> : <ChevronDown />}
                  </button>
                  <Layers className="object-type-icon folder" aria-hidden="true" />
                  <EditableField
                    aria-label={`Name of ${service.name}`}
                    value={service.name}
                    placeholder="Service Name"
                    disabled={system}
                    title={system ? 'Protected system service' : 'Service Name'}
                    iconTitle={system ? 'Protected system service (read-only)' : 'Editable Service Name'}
                    onClick={(event) => event.stopPropagation()}
                    onChange={(value) => w.updateService(service.id, { name: value })}
                  />
                  <EditableField
                    aria-label={`UUID of ${service.name}`}
                    value={service.uuid}
                    placeholder="0xEE00"
                    maxLength={6}
                    disabled={system}
                    title={system ? 'Protected system UUID (read-only)' : '16-bit Service UUID (0xXXXX)'}
                    iconTitle={system ? 'Protected system UUID (read-only)' : 'Editable UUID'}
                    onClick={(event) => event.stopPropagation()}
                    onChange={(value) => w.updateService(service.id, { uuid: normalizeUuid16(value) })}
                    onBlur={(event) => w.updateService(service.id, { uuid: padUuid16(event.target.value) })}
                  />
                  <span className="object-main-kind">{service.isPrimary ? 'Primary Service' : 'Service'}</span>
                  <div className="ble-col-action">
                    {system ? (
                      <Badge tone="system" className="ble-sys-badge" title="Core system service: locked on the board">SYS</Badge>
                    ) : (
                      <button type="button" className="object-main-delete" title="Delete service" aria-label={`Delete ${service.name}`} onClick={(event) => { event.stopPropagation(); w.removeService(service.id) }}>
                        <Trash2 />
                      </button>
                    )}
                  </div>
                </div>
                <div className="object-main-row object-main-bottom">
                  <span>{service.characteristics.length} {service.characteristics.length === 1 ? 'characteristic' : 'characteristics'}</span>
                </div>
              </div>

              {!collapsed && (
                <div className="object-main-children">
                  {service.characteristics.map((char) => {
                    const sys = isBoardCharacteristic(char)
                    return (
                      <div key={char.id} className="object-main-entry depth-1">
                        <div className={`object-main-card is-compact ble-char-card ${w.selectedId === char.id ? 'selected' : ''}`} onClick={() => w.select(char.id)}>
                          <div className="object-main-row object-main-top">
                            <Radio className="object-type-icon text" aria-hidden="true" />
                            <EditableField
                              aria-label={`Name of ${char.name}`}
                              value={char.name}
                              placeholder="Characteristic Name"
                              disabled={sys}
                              title={sys ? 'Protected system characteristic' : 'Characteristic Name'}
                              iconTitle={sys ? 'Protected system characteristic (read-only)' : 'Editable Characteristic Name'}
                              onClick={(event) => event.stopPropagation()}
                              onChange={(value) => w.updateCharacteristic(service.id, char.id, { name: value })}
                            />
                            <EditableField
                              aria-label={`UUID of ${char.name}`}
                              value={char.uuid}
                              placeholder="0xEE01"
                              maxLength={6}
                              disabled={sys}
                              title={sys ? 'Protected system UUID (read-only)' : '16-bit Characteristic UUID (0xXXXX)'}
                              iconTitle={sys ? 'Protected system UUID (read-only)' : 'Editable UUID'}
                              onClick={(event) => event.stopPropagation()}
                              onChange={(value) => w.updateCharacteristic(service.id, char.id, { uuid: normalizeUuid16(value) })}
                              onBlur={(event) => w.updateCharacteristic(service.id, char.id, { uuid: padUuid16(event.target.value) })}
                            />
                            <PropBadges char={char} />
                            <div className="ble-col-action">
                              {sys ? (
                                <Badge tone="system" className="ble-sys-badge" title="Core system characteristic">SYS</Badge>
                              ) : (
                                <button type="button" className="object-main-delete" title="Delete characteristic" aria-label={`Delete ${char.name}`} onClick={(event) => { event.stopPropagation(); w.removeCharacteristic(service.id, char.id) }}>
                                  <Trash2 />
                                </button>
                              )}
                            </div>
                          </div>
                        </div>
                      </div>
                    )
                  })}
                  {!system && (
                    <button type="button" className="ble-add-char-inline-btn" onClick={() => w.addCharacteristic(service.id)}>
                      <Plus aria-hidden="true" />
                      <span>Add Characteristic</span>
                    </button>
                  )}
                </div>
              )}
            </div>
          )
        })}
        {!w.profile.services.length && (
          <div className="object-editor-empty">
            <p>No BLE GATT services yet.</p>
            <div><button onClick={w.addService}>Add Service</button></div>
          </div>
        )}
      </div>
    </div>
  )
}

const BUFFER_PRESETS = [{ label: 'None', val: 0 }, { label: '96 B', val: 96 }, { label: '512 B', val: 512 }, { label: '2 KB', val: 2048 }]

function BufferRow({ label, hint, value, disabled, onChange }: { label: string; hint: string; value: number; disabled: boolean; onChange: (value: number) => void }) {
  return (
    <FormRow label={label} hint={hint}>
      <NumberField integer min={0} className="form-field-panel" value={value} disabled={disabled} onCommit={onChange} />
      <div className="ble-preset-chips">
        {BUFFER_PRESETS.map((preset) => (
          <button key={preset.val} type="button" disabled={disabled} className={`ble-preset-chip ${value === preset.val ? 'active' : ''}`} onClick={() => onChange(preset.val)}>{preset.label}</button>
        ))}
      </div>
    </FormRow>
  )
}

function ConnectorRelations({ connectors, uuid, onJump }: { connectors: readonly ConnectorSettings[]; uuid: string; onJump?: (key: string) => void }) {
  const bound = connectorsOnCharacteristic(connectors, uuid)
  if (!bound.length) return <p className="ble-connector-desc">No connector sends on this characteristic. Bind one under Data Connectors.</p>
  return (
    <div className="ble-related-chars">
      {bound.map((connector) => (
        <button key={connector.key} type="button" className="ble-related-char-button" title={`Open ${connector.alias}`} disabled={!onJump} onClick={() => onJump?.(connector.key)}>
          <Network className="object-type-icon folder" />
          <span className="ble-related-name">{connector.alias}{connector.isSuspended ? ' (paused)' : ''}</span>
          <Badge push>{connector.stream}</Badge>
          <ChevronRight />
        </button>
      ))}
    </div>
  )
}

export function BleDetails({ workspace: w, connectors, onJumpToConnector }: { workspace: BleSettings; connectors: readonly ConnectorSettings[]; onJumpToConnector?: (key: string) => void }) {
  const item = w.selectedItem

  if (!item || item.kind === 'general') {
    return <div className="object-details ble-details"><p className="object-tree-empty" style={{ padding: '24px 16px' }}>Select a service or a characteristic to see its details.</p></div>
  }

  if (item.kind === 'service') {
    const { service } = item
    const system = isBoardService(service)
    return (
      <div className="object-details ble-details">
        <PanelHeader icon={<Layers className="object-check-eye" />} title={service.name}>
          {system && <Badge tone="system">SYS</Badge>}
          <Badge push>{service.uuid}</Badge>
          {!system && <button type="button" className="object-main-delete" title="Delete service" aria-label={`Delete ${service.name}`} onClick={() => w.removeService(service.id)}><Trash2 /></button>}
        </PanelHeader>

        <div className="object-details-section">
          <h3>Service Options</h3>
          <Switch label="Primary Service" checked={service.isPrimary} disabled={system} onChange={(checked) => w.updateService(service.id, { isPrimary: checked })} />
          {system && <p className="ble-connector-desc">The runIT service carries the router link and is locked: its characteristics cannot be changed or removed. Advertising names this service.</p>}
        </div>

        <div className="object-details-section">
          <h3>Characteristics ({service.characteristics.length})</h3>
          <div className="ble-related-chars">
            {service.characteristics.map((char) => (
              <button key={char.id} className="ble-related-char-button" onClick={() => w.select(char.id)}>
                <Radio className="object-type-icon text" />
                <span className="ble-related-name">{char.name}</span>
                <Badge push>{char.uuid}</Badge>
                <ChevronRight />
              </button>
            ))}
            {!service.characteristics.length && <p>No characteristics in this service.</p>}
          </div>
        </div>

        <div className="object-details-section">
          <h3>Connectors sending here</h3>
          <div className="ble-connector-summary-list">
            {service.characteristics.map((char) => {
              const bound = connectorsOnCharacteristic(connectors, char.uuid)
              return (
                <div key={char.id} className="ble-connector-row">
                  <div className="ble-connector-char-info">
                    <span className="ble-connector-char-name">{char.name} ({char.uuid})</span>
                    <span className="ble-connector-target">{bound.length ? bound.map((connector) => connector.alias).join(', ') : 'No connector'}</span>
                  </div>
                </div>
              )
            })}
          </div>
        </div>
      </div>
    )
  }

  const { service, characteristic: char } = item
  const sys = isBoardCharacteristic(char)
  const set = (patch: Partial<BleCharacteristicSettings>) => w.updateCharacteristic(service.id, char.id, patch)
  const canTransmit = char.notify || char.indicate

  return (
    <div className="object-details ble-details">
      <PanelHeader icon={<Radio className="object-check-eye" />} title={char.name}>
        {sys && <Badge tone="system">SYS</Badge>}
        <Badge push>{char.uuid}</Badge>
        {!sys && <button type="button" className="object-main-delete" title="Delete characteristic" aria-label={`Delete ${char.name}`} onClick={() => w.removeCharacteristic(service.id, char.id)}><Trash2 /></button>}
      </PanelHeader>

      <div className="object-details-section">
        <h3>Properties</h3>
        <Switch label="Write (the client sends to the board)" checked={char.write} disabled={sys} onChange={(checked) => set({ write: checked, ...(!checked ? { rxBufferSize: 0 } : {}) })} />
        <Switch label="Notify" checked={char.notify} disabled={sys} onChange={(checked) => set({ notify: checked, ...(!checked && !char.indicate ? { txBufferSize: 0 } : {}) })} />
        <Switch label="Indicate" checked={char.indicate} disabled={sys} onChange={(checked) => set({ indicate: checked, ...(!checked && !char.notify ? { txBufferSize: 0 } : {}) })} />
        <Switch label="Framed (frames longer than one packet)" checked={char.framed} disabled={sys} onChange={(checked) => set({ framed: checked })} />
        <p className="ble-connector-desc">A framed characteristic starts every packet with a fragment header byte, so a frame up to {w.frameMax} B works at any MTU. The app adds and strips the byte.</p>
      </div>

      <div className="object-details-section">
        <h3>Connectors sending here</h3>
        <ConnectorRelations connectors={connectors} uuid={char.uuid} {...(onJumpToConnector ? { onJump: onJumpToConnector } : {})} />
      </div>

      {(canTransmit || char.write) && (
        <div className="object-details-section">
          <h3>Buffer Sizes</h3>
          {canTransmit && <BufferRow label="Transmit Buffer" hint="Items waiting to be sent; an item takes its length + 8 B. A no-split ring holds items of about half its size." value={char.txBufferSize} disabled={sys} onChange={(value) => set({ txBufferSize: value })} />}
          {char.write && <BufferRow label="Receive Buffer" hint="Peer writes queued for the program to read. 0 = none." value={char.rxBufferSize} disabled={sys} onChange={(value) => set({ rxBufferSize: value })} />}
        </div>
      )}

      <div className="object-details-section">
        <h3>Parent Service</h3>
        <button className="ble-related-char-button" onClick={() => w.select(service.id)}>
          <Layers className="object-type-icon folder" />
          <span className="ble-related-name">{service.name}</span>
          <Badge push>{service.uuid}</Badge>
          <ChevronRight />
        </button>
      </div>
    </div>
  )
}
