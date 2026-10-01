import { useState } from 'react'
import { Badge } from '../components/Badge'
import { Button } from '../components/Button'
import { PaletteSearch } from '../components/PaletteSearch'
import { PaletteSectionHeader } from '../components/PaletteSectionHeader'
import { ArrowLeftRight, ListChecks, Plus, Trash2 } from 'lucide-react'
import { TreeSlab } from '../components/TreeSlab'
import { boardDeviceRef } from '../domain/project'
import { DeviceTile } from './DeviceTile'
import { deviceDisplayName } from '../domain/devices'
import type { DevicesWorkspace } from './useDevicesWorkspace'

type Folder = 'system' | 'user' | 'actions'

const matches = (query: string, ...texts: (string | undefined)[]): boolean => {
  const q = query.trim().toLowerCase()
  return !q || texts.some((text) => text?.toLowerCase().includes(q))
}

/** Left panel of the Board view: the board's devices, the user's devices and the actions. */
export function DevicesPalette({ workspace: w, boardMode, onToggleMode }: {
  workspace: DevicesWorkspace
  /** The Board view's main screen: View / Manage (the device pages) or the board canvas. */
  boardMode: 'manage' | 'canvas'
  onToggleMode: () => void
}) {
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
  const user = w.devices.filter((device) => matches(query, deviceDisplayName(w.catalog, device), device.name, device.description, String(device.deviceId), w.catalog.type(device.type)?.title, ...device.tags))
  const actions = w.actions.filter((action) => matches(query, action.name, String(action.actionId)))

  const folder = (key: Folder, label: string, count: number, add?: { title: string; onClick: () => void }) => (
    <PaletteSectionHeader label={label} count={count} open={!collapsed.has(key)} onToggle={() => toggle(key)} addAction={add && { label: add.title, onClick: add.onClick }} />
  )

  return (
    <div className="code-palette devices-palette">
      <div className="devices-palette-body">
        <PaletteSearch value={query} onChange={setQuery} placeholder="Search devices & actions..." label="Search devices and actions" />

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
                  badges={<><Badge tone="system">SYS</Badge><Badge push>#{device.deviceId}</Badge></>}
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
                  label={deviceDisplayName(w.catalog, device)}
                  title={w.catalog.type(device.type)?.title}
                  badges={<>{w.diagnostics.some((entry) => entry.severity === 'error' && (entry.subjectId === device.id || entry.subjectId === `setup:${device.id}`)) && <span className="devices-error-dot" title="Has problems" />}<Badge push>#{device.deviceId}</Badge></>}
                  actions={<button type="button" className="tree-slab-action" title="Delete device" aria-label={`Delete ${deviceDisplayName(w.catalog, device)}`} onClick={(event) => { event.stopPropagation(); w.removeDevice(device.id) }}><Trash2 aria-hidden="true" /></button>}
                />
              ))}
              {!w.devices.length && <Button variant="dashed" block onClick={() => w.select({ kind: 'add' })}><Plus aria-hidden="true" />Add a device</Button>}
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
                  badges={<><Badge tone="count">{action.steps.length} steps</Badge><Badge>ID {action.actionId}</Badge></>}
                  actions={<button type="button" className="tree-slab-action" title="Delete action" aria-label={`Delete ${action.name}`} onClick={(event) => { event.stopPropagation(); w.removeAction(action.id) }}><Trash2 aria-hidden="true" /></button>}
                />
              ))}
            </div>
          )}
        </div>
      </div>
      <div className="code-mode-footer">
        <button className="code-mode-toggle" onClick={onToggleMode}><ArrowLeftRight aria-hidden="true" /><span>Switch to {boardMode === 'manage' ? 'Board canvas' : 'View / Manage'}</span></button>
      </div>
    </div>
  )
}
