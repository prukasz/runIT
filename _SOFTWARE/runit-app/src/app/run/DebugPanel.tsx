import { Bug, ChevronsRight, RotateCcw, StepForward } from 'lucide-react'
import { Button, Card, CardStack, FormRow, NumberField, PanelHeader, SegmentedSwitch } from '@ui/components'
import { SLOW_FACTORS } from '../useVm'
import type { Stepping } from '../useVm'
import { HOLD_MAX_MS, STEP_MS_MAX } from './useRunSettings'
import type { RunSettings } from './useRunSettings'
import './DebugPanel.css'

const STEPPING_OPTIONS = [['continuous', 'Continuous'], ['loop', 'Loop by loop'], ['step', 'Step by step']] as const satisfies readonly (readonly [Stepping, string])[]
const STEPPING_NOTE: Record<Stepping, string> = {
  continuous: 'The program runs pass after pass.',
  loop: 'The board runs one pass of the program (every block once) each time you ask: Next loop.',
  step: 'The board runs one block each time you ask: Next block.',
}

/**
 * The right panel of the bug button: start the program for debugging (every block's output is read back as well as the flow and
 * the variables), how it steps (all the time, a pass at a time, a block at a time: the VM's run modes), the slow motion and how
 * long a pulse stays lit. Once the program runs the choices act on it at once.
 */
export function DebugPanel({ settings, canStart, why, progress, stale, running, stepping, watching, onStart, onStepping, onNext, onRewind, onSlow, onStepMs }: {
  settings: RunSettings
  canStart: boolean
  /** Why Start is not available, as a note. */
  why?: string
  /** While a program is being loaded: how far. */
  progress?: string
  /** The program changed since it was loaded. */
  stale: boolean
  /** A program is started on the board. */
  running: boolean
  /** How the started program steps now. */
  stepping: Stepping | undefined
  /** The program on the board is the debug one (its outputs are read back). */
  watching: boolean
  onStart: () => void
  onStepping: (how: Stepping) => void
  onNext: () => void
  onRewind: () => void
  onSlow: (factor: number) => void
  /** The program time each pass asked for adds, ms. */
  onStepMs: (ms: number) => void
}) {
  const how = running && stepping ? stepping : settings.stepping
  return (
    <div className="debug-panel">
      <PanelHeader icon={<Bug aria-hidden="true" />} title="Debug" />
      <CardStack>
        <Card title="Program" help="Loads the program into the board and starts it, reading back every block's output, which blocks act and the variables.">
          <Button block variant="primary" disabled={!canStart} title={why ?? 'Load the program into the board and start it for debugging'} onClick={onStart}>
            <Bug aria-hidden="true" />{stale ? 'Load the changed program' : watching && running ? 'Load again' : 'Start debugging'}
          </Button>
          {(progress || why) && <p className="debug-panel-note">{progress ?? why}</p>}
        </Card>
        <Card title="Stepping" help="The VM's run modes: continuous, a pass at a time (scan), a block at a time (block).">
          <SegmentedSwitch label="How the program steps" value={how} options={STEPPING_OPTIONS} onChange={(next) => { settings.setStepping(next); if (running) onStepping(next) }} />
          <p className="debug-panel-note">{STEPPING_NOTE[how]}</p>
          <div className="debug-panel-actions">
            <Button disabled={!running || how === 'continuous'} title={how === 'step' ? 'Run the next block' : 'Run the next pass'} onClick={onNext}>
              {how === 'step' ? <StepForward aria-hidden="true" /> : <ChevronsRight aria-hidden="true" />}{how === 'step' ? 'Next block' : 'Next loop'}
            </Button>
            <Button disabled={!running} title="Back to the start of the program" onClick={onRewind}><RotateCcw aria-hidden="true" />Rewind</Button>
          </div>
          <FormRow label="Time per step (ms)" note="While you step, the program's own clock stands still between your requests, so Every, Timer and Periodic would never fire. Each pass you ask for moves that clock by this many milliseconds (0 stands time still).">
            <NumberField value={settings.stepMs} min={0} max={STEP_MS_MAX} integer onCommit={(ms) => { settings.setStepMs(ms); if (running) onStepMs(ms) }} aria-label="Time per step, milliseconds" />
          </FormRow>
        </Card>
        <Card title="Slow motion" help="How many times slower the board runs its passes, so each one can be followed.">
          <SegmentedSwitch label="Slow motion" value={String(settings.slow)} options={SLOW_FACTORS.map((f) => [String(f), f === 1 ? 'Off' : `${f}×`] as const)} onChange={(v) => { settings.setSlow(Number(v)); if (running) onSlow(Number(v)) }} />
        </Card>
        <Card title="Display" help="How the watched program is drawn, in the debug and in the live view (the eye).">
          <FormRow label="Keep a pulse lit for (ms)" note="How long a block that acted, or a value that changed, stays lit: a pass lasts a few milliseconds, too short to see.">
            <NumberField value={settings.holdMs} min={0} max={HOLD_MAX_MS} integer onCommit={settings.setHoldMs} aria-label="Keep a pulse lit for, milliseconds" />
          </FormRow>
        </Card>
      </CardStack>
    </div>
  )
}
