import { TextField } from '../components/FormField'
import { DEFAULT_HOLD_MS, MAX_HOLD_MS, setDebugHold, useDebugHold } from './debugPrefs'

/** Settings for the canvas debug view (General settings). */
export function DebugSettings() {
  const hold = useDebugHold()
  return (
    <div className="object-details-section">
      <h3>Debug view</h3>
      <label>
        Pulse hold (ms)
        <TextField type="number" min={0} max={MAX_HOLD_MS} step={50} value={hold} onChange={(event) => setDebugHold(Number(event.target.value))} />
      </label>
      <p className="program-muted">
        A tick, and a block acting on it, are true for a single VM pass, too short to catch on screen. A block whose ENO or enable was seen true stays green this long.
        Default {DEFAULT_HOLD_MS}; 0 shows only the latest value. Remembered in this browser.
      </p>
    </div>
  )
}
