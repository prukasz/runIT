import { useRef } from 'react'
import { TextField } from '../FormField'

/** Uncontrolled while editing so a parent update cannot overwrite typed text. */
export function InlineRename({ value, label, placeholder, className, selectOnFocus = false, maxLength, onCommit, onCancel }: {
  value: string
  label: string
  placeholder?: string
  className?: string
  selectOnFocus?: boolean
  maxLength?: number
  onCommit: (value: string) => void
  onCancel: () => void
}) {
  const finished = useRef(false)
  const commit = (next: string) => {
    if (finished.current) return
    finished.current = true
    onCommit(next)
  }
  const cancel = () => {
    if (finished.current) return
    finished.current = true
    onCancel()
  }

  return <TextField className={className} autoFocus defaultValue={value} placeholder={placeholder} maxLength={maxLength} aria-label={label}
    onFocus={(event) => { if (selectOnFocus) event.currentTarget.select() }}
    onPointerDown={(event) => event.stopPropagation()}
    onClick={(event) => event.stopPropagation()}
    onDoubleClick={(event) => event.stopPropagation()}
    onKeyDown={(event) => {
      event.stopPropagation()
      if (event.key === 'Enter') { event.preventDefault(); commit(event.currentTarget.value) }
      if (event.key === 'Escape') { event.preventDefault(); cancel() }
    }}
    onBlur={(event) => commit(event.currentTarget.value)} />
}
