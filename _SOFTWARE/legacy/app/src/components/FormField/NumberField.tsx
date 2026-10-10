import { useEffect, useState } from 'react'
import type { InputHTMLAttributes } from 'react'
import { TextField } from './FormField'

/**
 * A number field you can type in freely. The text is the field's own while it has focus: a value is committed to the
 * owner as soon as it is a number inside `min` / `max`, and a half-typed one ("1" on the way to "60", an empty box, a
 * lone "-") is left alone instead of being clamped under your fingers. On blur the box shows the committed value again.
 */
export function NumberField({ value, onCommit, min, max, ...rest }: Omit<InputHTMLAttributes<HTMLInputElement>, 'value' | 'onChange' | 'type' | 'min' | 'max'> & {
  value: number
  onCommit: (value: number) => void
  min?: number
  max?: number
}) {
  const [text, setText] = useState(String(value))
  const [editing, setEditing] = useState(false)
  useEffect(() => {
    if (!editing) setText(String(value))
  }, [value, editing])

  const valid = (candidate: string): boolean => {
    const number = Number(candidate)
    return candidate.trim() !== '' && Number.isFinite(number) && (min === undefined || number >= min) && (max === undefined || number <= max)
  }

  return (
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
  )
}
