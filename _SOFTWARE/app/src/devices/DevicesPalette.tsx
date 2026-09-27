import { useState } from 'react'
import { ChevronDown, ChevronRight, ListChecks, Plus, Search, Trash2, X } from 'lucide-react'
import { TreeSlab } from '../components/TreeSlab'
import { boardDeviceRef } from '../domain/project'
import { DeviceTile } from './DeviceTile'
import type { DevicesWorkspace } from './useDevicesWorkspace'

const tabs = ['Devices', 'Features'] as const
type Tab = typeof tabs[number]
type Folder = 'system' | 'user' | 'actions'

const matches = (query: string, ...texts: (string | undefined)[]): boolean => {
  const q = query.trim().toLowerCase()
  return !q || texts.some((text) => text?.toLowerCase().includes(q))
}

/** Left panel of the Board view: the board's devices, the user's devices and the actions. */
export function DevicesPalette({ workspace: w }: { workspace: DevicesWorkspace }) {
  const [tab, setTab] = useState<Tab>('Devices')
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<Folder>>(new Set())
  const toggle = (folder: Folder) => setCollapsed((current) => {
    const next = new Set(current)
    if (next.has(folder)) next.delete(folder)
    else next.add(folder)
    return next
  })
  const selectedRef = w.selection?.kind === 'device' ? w.selection.ref : undefined

  const board = w.catalog.board.filter((device) => matches(query, device.name, device.title, String(device.deviceId), ...(device.type?.tags ?? [])))
  const user = w.devices.filter((device) => matches(query, device.name, device.description, String(device.deviceId), w.catalog.type(device.type)?.title, ...device.tags))
  const actions = w.actions.filter((action) => matches(query, action.name, String(action.actionId)))

  const folder = (key: Folder, label: string, count: number, add?: { title: string; onClick: () => void }) => (
    <div className="devices-folder-heading">
      <button type="button" className="devices-folder-toggle" onClick={() => toggle(key)} aria-expanded={!collapsed.has(key)}>
        {collapsed.has(key) ? <ChevronRight aria-hidden="true" /> : <ChevronDown aria-hidden="true" />}
        <span>{label}</span>
        <em>{count}</em>
      </button>
      {add && <button type="button" className="devices-folder-add" title={add.title} aria-label={add.title} onClick={add.onClick}><Plus aria-hidden="true" /></button>}
    </div>
  )

  return (
    <div className="code-palette devices-palette">
      <div className="code-palette-tabs" role="tablist" aria-label="Board palettes">
        {tabs.map((entry) => <button key={entry} role="tab" aria-selected={tab === entry} className={tab === entry ? 'selected' : ''} onClick={() => setTab(entry)}>{entry}</button>)}
      </div>

      {tab === 'Features' ? (
        <div className="devices-palette-body">
          <p className="devices-muted">Features (servo, H-bridge motor …) build on devices. They show here once the firmware publishes their descriptors (features.md F-GAP-2).</p>
        </div>
      ) : (
        <div className="devices-palette-body">
          <div className="object-tree-search-bar">
            <Search className="search-icon" aria-hidden="true" />
            <input type="text" placeholder="Search devices & actions..." aria-label="Search devices and actions" value={query} onChange={(event) => setQuery(event.target.value)} onKeyDown={(event) => { if (event.key === 'Escape') setQuery('') }} />
            {query && <button type="button" className="search-clear-btn" title="Clear search" aria-label="Clear search" onClick={() => setQuery('')}><X aria-hidden="true" /></button>}
          </div>

          <div className="object-tree-scroll" role="navigation" aria-label="Devices and actions">
            {folder('system', 'System devices', board.length)}
            {!collapsed.has('system') && (
              <div className="devices-folder-items">
                {board.map((device) => (
                  <TreeSlab
                    key={device.deviceId}
                    selected={selectedRef === boardDeviceRef(device.deviceId)}
                    onClick={() => w.select({ kind: 'device', ref: boardDeviceRef(device.deviceId) })}
                    icon={<DeviceTile type={device.type} size="small" />}
                    label={device.name}
                    title={device.title}
                    badges={<><span className="conn-system-label">SYS</span><span className="ble-uuid-chip">#{device.deviceId}</span></>}
                  />
                ))}
              </div>
            )}

            {folder('user', 'User devices', user.length, { title: 'Add a device', onClick: () => w.select({ kind: 'add' }) })}
            {!collapsed.has('user') && (
              <div className="devices-folder-items">
                {user.map((device) => (
                  <TreeSlab
                    key={device.id}
                    selected={selectedRef === device.id}
                    isMatch={!!query.trim()}
                    onClick={() => w.select({ kind: 'device', ref: device.id })}
                    icon={<DeviceTile appearance={device.appearance} type={w.catalog.type(device.type)} size="small" />}
                    label={device.name}
                    title={w.catalog.type(device.type)?.title}
                    badges={<>{w.diagnostics.some((entry) => entry.severity === 'error' && (entry.subjectId === device.id || entry.subjectId === `setup:${device.id}`)) && <span className="devices-error-dot" title="Has problems" />}<span className="ble-uuid-chip">#{device.deviceId}</span></>}
                    actions={<button type="button" className="tree-slab-action" title="Delete device" aria-label={`Delete ${device.name}`} onClick={(event) => { event.stopPropagation(); w.removeDevice(device.id) }}><Trash2 aria-hidden="true" /></button>}
                  />
                ))}
                {!w.devices.length && <button type="button" className="devices-empty-add" onClick={() => w.select({ kind: 'add' })}><Plus aria-hidden="true" />Add a device</button>}
              </div>
            )}

            {folder('actions', 'Actions', actions.length, { title: 'New action', onClick: () => w.compose() })}
            {!collapsed.has('actions') && (
              <div className="devices-folder-items">
                {actions.map((action) => (
                  <TreeSlab
                    key={action.id}
                    selected={w.selection?.kind === 'action' && w.selection.id === action.id}
                    onClick={() => w.select({ kind: 'action', id: action.id })}
                    onDoubleClick={() => w.compose(action.id)}
                    icon={<span className="object-type-icon text"><ListChecks aria-hidden="true" /></span>}
                    label={action.name}
                    badges={<><span className="ble-uuid-chip">{action.steps.length} steps</span><span className="ble-uuid-chip">ID {action.actionId}</span></>}
                    actions={<button type="button" className="tree-slab-action" title="Delete action" aria-label={`Delete ${action.name}`} onClick={(event) => { event.stopPropagation(); w.removeAction(action.id) }}><Trash2 aria-hidden="true" /></button>}
                  />
                ))}
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  )
}
