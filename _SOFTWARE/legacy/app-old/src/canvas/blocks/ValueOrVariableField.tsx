import { useState } from 'react'
import { TextField } from '../../components/FormField'
import type { ObjectPath } from '../../domain/project'

const NUMBER = /^[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?$/i

/**
 * One field for a setting a pin can replace: type a constant, or a variable (a name path such as `motor.speed[2]`), or drop one on it.
 * A constant sets the setting and unwires the pin; a variable wires the pin; empty clears whichever it held.
 */
export function ValueOrVariableField({ label, hint, constant, variable, variableText, integer, min, parse, onConstant, onVariable, onDropVariable }: {
  label: string
  hint?: string
  constant: number | undefined
  /** What the pin is wired to, when it is (shown instead of the constant). */
  variable?: ObjectPath
  variableText?: string
  integer: boolean
  min?: number
  /** Text → variable path; throws with a readable reason. */
  parse: (text: string) => ObjectPath
  onConstant: (value: number | undefined) => void
  onVariable: (path: ObjectPath) => void
  onDropVariable?: (data: DataTransfer) => void
}) {
  const shown = variable ? variableText ?? variable.root : constant === undefined ? '' : String(constant)
  const [problem, setProblem] = useState<string | undefined>()
  const commit = (raw: string) => {
    const text = raw.trim()
    if (text === shown) { setProblem(undefined); return }
    if (text === '') { setProblem(undefined); onConstant(undefined); return }
    if (NUMBER.test(text)) {
      const value = Number(text)
      if (integer && !Number.isInteger(value)) { setProblem('A whole number is needed.'); return }
      if (min !== undefined && value < min) { setProblem(`At least ${min}.`); return }
      setProblem(undefined)
      onConstant(value)
      return
    }
    try {
      onVariable(parse(text))
      setProblem(undefined)
    } catch (error) {
      setProblem(error instanceof Error ? error.message : 'Not a number or a variable.')
    }
  }
  return (
    <label
      className="block-field"
      onDragOver={onDropVariable ? (event) => { event.preventDefault(); event.dataTransfer.dropEffect = 'copy' } : undefined}
      onDrop={onDropVariable ? (event) => { event.preventDefault(); onDropVariable(event.dataTransfer) } : undefined}
    >
      <span>{label}</span>
      <TextField
        key={shown}
        type="text"
        className={variable ? 'is-variable' : undefined}
        inputMode="decimal"
        defaultValue={shown}
        placeholder="0 or a variable"
        aria-invalid={problem ? true : undefined}
        onKeyDown={(event) => { if (event.key === 'Enter') event.currentTarget.blur() }}
        onBlur={(event) => commit(event.currentTarget.value)}
      />
      {problem ? <em className="block-field-problem">{problem}</em> : hint && <em>{hint}</em>}
    </label>
  )
}
