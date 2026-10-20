import { useEffect, useState } from 'react'
import './Slider.css'

/**
 * A number on a scale: drag the thumb, or use the arrow keys, Home and End. `onChange` is called when the thumb is let go (or a key
 * is pressed), not on every step of a drag, so a value that reshapes the screen (the interface scale) does not move the slider
 * under the pointer. While dragging, `format` shows the value under the thumb.
 */
export function Slider({ value, min, max, step, onChange, format = String, label, disabled, error }: {
  /** `undefined`: nothing is set yet; the thumb waits at `min` and the value reads "—" until it is moved or clicked. */
  value: number | undefined
  min: number
  max: number
  step: number
  onChange: (value: number) => void
  /** The value as text, shown beside the slider. */
  format?: (value: number) => string
  label: string
  disabled?: boolean
  /** The value is needed and missing: the track is painted red. */
  error?: boolean
}) {
  const [draft, setDraft] = useState(value)
  const [moved, setMoved] = useState(false)
  useEffect(() => { setDraft(value); setMoved(false) }, [value])
  const commit = () => { if (moved || (value === undefined && draft !== undefined)) onChange(draft ?? min); setMoved(false) }
  const shown = Math.min(max, Math.max(min, draft ?? min))
  return (
    <div className={`slider ${disabled ? 'is-disabled' : ''} ${value === undefined && draft === undefined ? 'is-unset' : ''} ${error ? 'is-error' : ''}`}>
      <input type="range" aria-label={label} min={min} max={max} step={step} value={shown} disabled={disabled}
        style={{ '--slider-fill': `${((shown - min) / (max - min)) * 100}%` } as React.CSSProperties}
        onChange={(event) => { setDraft(Number(event.target.value)); setMoved(true) }}
        onPointerDown={() => { if (value === undefined && draft === undefined) setDraft(min) }} onPointerUp={commit} onKeyUp={commit} onBlur={commit} />
      <span className="slider-value">{value === undefined && draft === undefined ? '—' : format(shown)}</span>
    </div>
  )
}
