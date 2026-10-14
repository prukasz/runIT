import { useState } from 'react'
import { Play, Plus, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Button } from '../components/Button'
import { matchesSearch, PaletteSearch } from '../components/PaletteSearch'
import { PaletteSectionHeader } from '../components/PaletteSectionHeader'
import { TreeSlab } from '../components/TreeSlab'
import { BOARD_DEVICES, deviceType } from '../domain/devices'
import { DeviceTile } from './DeviceTile'
import { boardRef } from './useDevicesWorkspace'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import './Devices.css'

type Folder = 'system' | 'user' | 'actions'

/** Left panel of the Devices tab: the board's devices and the user's, searched by name, type, tag or ID (as the legacy app). */
export function DevicesPalette({ workspace: w }: { workspace: DevicesWorkspace }) {
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<Folder>>(new Set())
  const toggle = (folder: Folder) => setCollapsed((current) => {
    const next = new Set(current)
    if (!next.delete(folder)) next.add(folder)
    return next
  })
  const hit = (...texts: (string | undefined)[]) => !query.trim() || texts.some((text) => !!text && matchesSearch(text, query))
  const selected = w.selection.kind === 'device' ? w.selection.ref : undefined

  const actions = w.actions.filter((action) => hit(action.name, String(action.actionId)))
  const board = BOARD_DEVICES.filter((device) => hit(w.boardName(device.id), device.alias, device.type?.alias, String(device.id), ...(device.type?.tags ?? [])))
  const user = w.devices.filter((device) => hit(device.name, device.description, deviceType(device.type)?.alias, String(device.deviceId), ...device.tags))

  return (
    <div className="devices-palette">
      <div className="devices-palette-body">
        <PaletteSearch value={query} onChange={setQuery} placeholder="Search devices..." label="Search devices" />
        <div className="object-tree-scroll" role="navigation" aria-label="Devices">
          <PaletteSectionHeader label="System devices" count={board.length} open={!collapsed.has('system')} onToggle={() => toggle('system')} />
          {!collapsed.has('system') && (
            <div className="devices-folder-items">
              {board.map((device) => (
                <TreeSlab
                  key={device.id}
                  selected={selected === boardRef(device.id)}
                  onClick={() => w.select({ kind: 'device', ref: boardRef(device.id) })}
                  icon={<DeviceTile type={device.type} size="small" />}
                  label={w.boardName(device.id)}
                  title={device.type?.alias}
                  badges={<><Badge tone="system">SYS</Badge><Badge push>#{device.id}</Badge></>}
                />
              ))}
            </div>
          )}

          <PaletteSectionHeader label="User devices" count={user.length} open={!collapsed.has('user')} onToggle={() => toggle('user')} addAction={{ label: 'Add a device', onClick: () => w.select({ kind: 'add' }) }} />
          {!collapsed.has('user') && (
            <div className="devices-folder-items">
              {user.map((device) => (
                <TreeSlab
                  key={device.id}
                  selected={selected === device.id}
                  onClick={() => w.select({ kind: 'device', ref: device.id })}
                  icon={<DeviceTile type={deviceType(device.type)} size="small" />}
                  label={device.name}
                  title={deviceType(device.type)?.alias ?? device.type}
                  badges={<>{w.problems.some((problem) => problem.deviceId === device.id) && <span className="devices-error-dot" title="Has problems" />}<Badge push>#{device.deviceId}</Badge></>}
                  actions={<button type="button" className="tree-slab-action" title="Delete device" aria-label={`Delete ${device.name}`} onClick={(event) => { event.stopPropagation(); w.removeDevice(device.id) }}><Trash2 aria-hidden="true" /></button>}
                />
              ))}
              {!w.devices.length && <Button variant="dashed" block onClick={() => w.select({ kind: 'add' })}><Plus aria-hidden="true" />Add a device</Button>}
            </div>
          )}

          <PaletteSectionHeader label="Actions" count={actions.length} open={!collapsed.has('actions')} onToggle={() => toggle('actions')} addAction={{ label: 'Add an action', onClick: () => w.addAction() }} />
          {!collapsed.has('actions') && (
            <div className="devices-folder-items">
              {actions.map((action) => (
                <TreeSlab
                  key={action.id}
                  selected={w.selection.kind === 'action' && w.selection.id === action.id}
                  onClick={() => w.select({ kind: 'action', id: action.id })}
                  icon={<span className="device-tile is-small"><Play aria-hidden="true" /></span>}
                  label={action.name}
                  badges={<>{w.stepProblems.some((problem) => problem.subject === action.id) && <span className="devices-error-dot" title="Has problems" />}<Badge push>#{action.actionId}</Badge></>}
                  actions={<button type="button" className="tree-slab-action" title="Delete action" aria-label={`Delete ${action.name}`} onClick={(event) => { event.stopPropagation(); w.removeAction(action.id) }}><Trash2 aria-hidden="true" /></button>}
                />
              ))}
              {!w.actions.length && <Button variant="dashed" block onClick={() => w.addAction()}><Plus aria-hidden="true" />Add an action</Button>}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
