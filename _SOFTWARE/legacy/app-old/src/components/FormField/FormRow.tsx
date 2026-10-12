import './FormField.css'
import type { ReactNode } from 'react'
import { FieldLabel } from '../FieldNote'

/**
 * A label above one control, with an optional (i) note and a hint line under the control.
 * `htmlFor` or `note` render the label through FieldLabel; a bare label stays a plain <label>.
 * Group rows in a `FormGrid` for two columns.
 */
export function FormRow({ label, htmlFor, note, hint, className = '', children }: { label: ReactNode; htmlFor?: string; note?: string; hint?: ReactNode; className?: string; children?: ReactNode }) {
  return (
    <div className={`form-row ${className}`.trim()}>
      {htmlFor || note ? <FieldLabel htmlFor={htmlFor} note={note}>{label}</FieldLabel> : <label>{label}</label>}
      {children}
      {hint && <span className="form-hint">{hint}</span>}
    </div>
  )
}

/** Two columns of form rows; one column on a narrow container. */
export function FormGrid({ className = '', children }: { className?: string; children?: ReactNode }) {
  return <div className={`form-grid ${className}`.trim()}>{children}</div>
}
