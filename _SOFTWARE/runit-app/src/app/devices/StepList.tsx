import { ArrowDown, ArrowUp } from 'lucide-react'
import { DeviceTile, RefPill, TreeAction, TreeSlab } from '@ui/components'
import { deviceIcon } from '@ui/components'
import type { Step } from '../project/projectFile'
import { describeStep } from './devices'
import type { DevicesState } from './useDevices'

/** A list of commands (a device's start commands, an action): each in one line, the device it goes to, moved up or down, removed. */
export function StepList({ steps, state, onChange, showDevice = true, empty }: {
  steps: readonly Step[]
  state: DevicesState
  onChange: (steps: readonly Step[]) => void
  /** The device of each step as a pill (an action); a device's own list leaves it out. */
  showDevice?: boolean
  empty: string
}) {
  if (steps.length === 0) return <p className="devices-hint">{empty}</p>
  const move = (i: number, by: -1 | 1) => {
    const list = [...steps]
    ;[list[i], list[i + by]] = [list[i + by]!, list[i]!]
    onChange(list)
  }
  return (
    <div className="devices-steps">
      {steps.map((step, i) => {
        const line = describeStep(state.catalog, step, state.nameOf, state.pinName)
        const device = line.device === undefined ? undefined : state.devices.find((d) => d.id === line.device)
        return (
          <TreeSlab key={i} twoRow icon={<DeviceTile size="small" tags={device?.type?.tags} />} label={line.command} title={line.args ? `${line.command} · ${line.args}` : line.command}
            badges={line.args ? <span className="devices-step-args">{line.args}</span> : undefined}
            refSlot={showDevice && line.device !== undefined
              ? <RefPill compact name={device?.name ?? `Device ${line.device}`} broken={!device} icon={deviceIcon(device?.type?.tags)} refKey={`device:${line.device}`}
                  onJump={device ? () => state.select({ kind: 'device', id: device.id }) : undefined} />
              : undefined}
            actions={
              <>
                {i > 0 && <TreeAction label="Move up" icon={ArrowUp} onClick={() => move(i, -1)} />}
                {i < steps.length - 1 && <TreeAction label="Move down" icon={ArrowDown} onClick={() => move(i, 1)} />}
                <TreeAction label="Remove" onClick={() => onChange(steps.filter((_, j) => j !== i))} />
              </>
            } />
        )
      })}
    </div>
  )
}
