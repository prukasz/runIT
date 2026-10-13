import { useEffect, useState } from 'react'
import type { InputHTMLAttributes } from 'react'
import { ChevronDown, ChevronUp } from 'lucide-react'
import { TextField } from './FormField'
import './NumberField.css'

/**
 * A number field you can type in freely. The text is the field's own while it has focus: a value is committed to the
 * owner as soon as it is a number inside `min` / `max`, and a half-typed one ("1" on the way to "60", an empty box, a
 * lone "-") is left alone instead of being clamped under your fingers. On blur the box shows the committed value again.
 */
export function NumberField({ value, onCommit, min, max, integer, ...rest }: Omit<InputHTMLAttributes<HTMLInputElement>, 'value' | 'onChange' | 'type' | 'min' | 'max'> & {
  value: number
  onCommit: (value: number) => void
  min?: number
  max?: number
  /** Only whole numbers are committed ("1.5" waits until it is a whole number). */
  integer?: boolean
}) {
  const [text, setText] = useState(String(value))
  const [editing, setEditing] = useState(false)
  useEffect(() => {
    if (!editing) setText(String(value))
  }, [value, editing])

  const valid = (candidate: string): boolean => {
    const number = Number(candidate)
    return candidate.trim() !== '' && Number.isFinite(number) && (!integer || Number.isInteger(number)) && (min === undefined || number >= min) && (max === undefined || number <= max)
  }

  const step = (direction: 1 | -1) => {
    const base = valid(text) ? Number(text) : value
    const next = Math.min(max ?? Infinity, Math.max(min ?? -Infinity, base + direction))
    setText(String(next))
    onCommit(next)
  }

  return (
    <div className="number-field">
      <TextField
        {...rest}
        type="number"
        min={min}
        max={max}
        value={text}
        onFocus={(event) => {
          setEditing(true)
          rest.onFocus?.(event)
        }}
        onChange={(event) => {
          setText(event.target.value)
          if (valid(event.target.value)) onCommit(Number(event.target.value))
        }}
        onBlur={(event) => {
          setEditing(false)
          setText(String(value))
          rest.onBlur?.(event)
        }}
      />
      <button type="button" tabIndex={-1} className="number-field-step is-up" aria-label="Increase" disabled={rest.disabled} onMouseDown={(event) => event.preventDefault()} onClick={() => step(1)}>
        <ChevronUp aria-hidden="true" />
      </button>
      <button type="button" tabIndex={-1} className="number-field-step is-down" aria-label="Decrease" disabled={rest.disabled} onMouseDown={(event) => event.preventDefault()} onClick={() => step(-1)}>
        <ChevronDown aria-hidden="true" />
      </button>
    </div>
  )
}
