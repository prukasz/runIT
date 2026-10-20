import { useEffect, useState } from 'react'
import { ListChecks } from 'lucide-react'
import { Badge, Button, DeviceTile, PaletteSectionHeader, SegmentedSwitch, TreeAction, TreeSlab } from '@ui/components'
import { usePersistedChoice } from '@ui/hooks/useStorage'
import type { DeviceInfo } from './devices'
import type { DevicesState, LiveDevice } from './useDevices'
import { useErrors } from '../console/useConsole'

/** What the board answered about a device, as a badge: on the board, suspended, of another type, not there. */
export function LiveBadge({ device, live, connected }: { device: DeviceInfo; live: LiveDevice | undefined; connected: boolean }) {
  if (!connected || !live) return null
  if (live.state === 'missing') return <Badge tone={device.system ? 'danger' : 'neutral'} title="The board has no device with this ID">not installed</Badge>
  if (device.type?.typeId !== undefined && live.typeId !== device.type.typeId) return <Badge tone="danger" title="The board has a device of another type under this ID">other type</Badge>
  if (live.suspended) return <Badge tone="warning" title="Suspended: its outputs are safe and it refuses writes until resumed">suspended</Badge>
  // A board device is on the board by definition: only a state that differs from that is shown.
  if (device.system) return null
  return <Badge tone="success" title="On the board">on board</Badge>
}

function DeviceRow({ device, state }: { device: DeviceInfo; state: DevicesState }) {
  const selected = state.selection?.kind === 'device' && state.selection.id === device.id
  // The board's errors about the device or a pin of it, not read yet: the row is red and counts them.
  const unread = useErrors()?.console.unread(`device:${device.id}`) ?? 0
  return (
    <TreeSlab
      twoRow
      icon={<DeviceTile size="small" tags={device.type?.tags} />}
      label={device.name}
      title={`${device.name} · ${device.type?.label ?? 'unknown type'}`}
      selected={selected}
      refKey={`device:${device.id}`}
      isError={unread > 0}
      badges={<><LiveBadge device={device} live={state.live.get(device.id)} connected={state.connected} />{unread > 0 && <Badge tone="danger" size="compact" title={`${unread} errors not read (Console)`}>{unread}</Badge>}</>}
      onClick={() => state.select({ kind: 'device', id: device.id })}
    />
  )
}

/**
 * The left panel of the Devices view: the devices (the board's own and the project's, with what the board answers about each)
 * and Add; or the actions and Add.
 */
export function DevicesExplorer({ state }: { state: DevicesState }) {
  const [tab, setTab] = usePersistedChoice('runit.devices.tab', ['devices', 'actions'] as const, 'devices')
  const [open, setOpen] = useState({ board: false, project: true })
  const system = state.devices.filter((d) => d.system)
  const own = state.devices.filter((d) => !d.system)
  const actions = state.selection?.kind === 'action' ? state.selection.id : undefined
  // What is selected is shown in its list: an action chosen elsewhere (Add to action, a pill) switches to the actions.
  const kind = state.selection?.kind
  useEffect(() => {
    if (kind) setTab(kind === 'action' ? 'actions' : 'devices')
  }, [kind, actions])

  return (
    <div className="devices-explorer">
      <SegmentedSwitch label="Devices or actions" value={tab} options={[['devices', 'Devices'], ['actions', 'Actions']]} onChange={setTab} />
      {tab === 'devices'
        ? (
          <>
            <PaletteSectionHeader label="Board" count={system.length} open={open.board} onToggle={() => setOpen((o) => ({ ...o, board: !o.board }))} />
            {open.board && system.map((d) => <DeviceRow key={d.id} device={d} state={state} />)}
            <PaletteSectionHeader label="Project" count={own.length} open={open.project} onToggle={() => setOpen((o) => ({ ...o, project: !o.project }))} />
            {open.project && own.map((d) => <DeviceRow key={d.id} device={d} state={state} />)}
            <Button variant="dashed" block onClick={() => state.select({ kind: 'add' })}>Add device</Button>
          </>
        )
        : (
          <>
            {state.project.actions.map((a) => (
              <TreeSlab key={a.id} icon={<DeviceTile size="small" icon={ListChecks} />} label={a.name} selected={actions === a.id}
                actions={<TreeAction label={`Delete ${a.name}`} onClick={() => state.removeAction(a.id)} />}
                onClick={() => state.select({ kind: 'action', id: a.id })} />
            ))}
            <Button variant="dashed" block onClick={() => state.select({ kind: 'action', id: state.addAction() })}>Add action</Button>
          </>
        )}
    </div>
  )
}
