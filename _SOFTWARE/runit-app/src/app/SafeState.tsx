import { Play, ShieldAlert, ShieldCheck } from 'lucide-react'
import { Button } from '@ui/components'
import type { SafeState } from './useVm'

/*
 * The board's safe state (useVm: `SYS SAFE`), shown where the user looks while it lasts: the right panel blinks red, and its top
 * says why the board entered it (the reason's alias and description: Stop, the dead-man, a lost link, a power fault), for how long,
 * and offers the way out (Resume: the devices resumed, the program started again when one is ready). A latched device fault refuses
 * every start until it is acknowledged. Collapsed, the panel shows a red shield that opens it.
 */

const since = (ms: number): string => (ms < 60_000 ? `${Math.round(ms / 1000)} s` : ms < 3_600_000 ? `${Math.floor(ms / 60_000)} min ${Math.round((ms % 60_000) / 1000)} s` : `${Math.floor(ms / 3_600_000)} h ${Math.floor((ms % 3_600_000) / 60_000)} min`)

export function SafeStateNotice({ safe, faultLatched, faultDevice, busy, programReady, onLeave, onAckFault }: {
  safe: SafeState
  faultLatched: boolean
  /** The latched fault's device by name. */
  faultDevice?: string
  busy: boolean
  /** A ready program is loaded: Resume starts it again. */
  programReady: boolean
  onLeave: () => void
  onAckFault: () => void
}) {
  return (
    <div className="safe-state" role="alert" data-help="The board is in its safe state: the program stopped, every device output off. Resume brings the devices back (motors move again) and starts the program again.">
      <div className="safe-state-head">
        <ShieldAlert aria-hidden="true" />
        <strong>Safe state</strong>
        <span className="safe-state-since" title="Since the board entered it">{since(safe.forMs)}</span>
      </div>
      <p className="safe-state-reason"><strong>{safe.alias}</strong>{safe.description ? `: ${safe.description}` : ''}</p>
      {faultLatched && <p className="safe-state-fault">A device fault is latched{faultDevice ? ` (${faultDevice})` : ''}: the program cannot start until it is acknowledged. The Console has what happened.</p>}
      <div className="safe-state-actions">
        {faultLatched && <Button size="sm" disabled={busy} title="Acknowledge the latched fault (VM EXEC ack fault): the program may start again" onClick={onAckFault}><ShieldCheck aria-hidden="true" />Acknowledge the fault</Button>}
        <Button size="sm" variant="primary" disabled={busy || faultLatched} title={programReady ? 'Resume every device, then start the program again: motors move again' : 'Resume every device: motors move again'} onClick={onLeave}>
          <Play aria-hidden="true" />{programReady ? 'Resume and run' : 'Resume the devices'}
        </Button>
      </div>
    </div>
  )
}

/** The collapsed panel's sign of the safe state: a red shield that opens the panel, where its reason and Resume are. */
export function SafeStateButton({ safe, onOpen }: { safe: SafeState; onOpen: () => void }) {
  return (
    <button type="button" className="safe-state-button" aria-label="Safe state" title={`Safe state: ${safe.alias} (${since(safe.forMs)}). Open to see why and to resume.`} onClick={onOpen}>
      <ShieldAlert aria-hidden="true" />
    </button>
  )
}
