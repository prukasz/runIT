import { ListableField } from '../ListableField'
import type { ListableFieldProps } from '../ListableField'

/** One thing to pick: a device, a pin, a channel, an action, a mode. The caller decides where the list comes from and what it is filtered by. */
export interface Choice<V extends string | number = number> {
  readonly value: V
  readonly label: string
  /** Hover text. */
  readonly description?: string
  readonly disabled?: boolean
  /** Choices sharing a group are listed under its heading. */
  readonly group?: string
}

export interface ChoiceFieldProps<V extends string | number> extends Omit<ListableFieldProps, 'value' | 'onChange' | 'options'> {
  /** The stored choice; undefined while nothing is chosen. */
  value: V | undefined
  choices: readonly Choice<V>[]
  /** Chosen: the choice's own value; the empty option (`emptyLabel`) gives undefined. */
  onChange: (value: V | undefined) => void
  /** An option that stands for "nothing" (Any, No action), always listed: it clears the choice. */
  emptyLabel?: string
  /** A prompt (Select a device) shown only while nothing is chosen; it can't be picked again, so onChange only gets real choices. */
  placeholder?: string
  /** Names a stored value the list no longer offers (it stays selectable, so the field never lies about what is stored). */
  unavailableLabel?: (value: V) => string
}

/**
 * The one dropdown for choosing from a list of things (devices, pins, channels, events, actions, modes, ...), so each of them looks and
 * behaves alike: same field, an optional empty option, a stored value that is no longer offered kept visible, descriptions on hover.
 * Built on ListableField; labels and hints around it belong to the caller.
 */
export function ChoiceField<V extends string | number = number>({ value, choices, onChange, emptyLabel, placeholder, unavailableLabel, ...rest }: ChoiceFieldProps<V>) {
  const missing = value !== undefined && !choices.some((choice) => choice.value === value)
  const options = [
    ...(emptyLabel !== undefined ? [{ value: '', label: emptyLabel }] : value === undefined ? [{ value: '', label: placeholder ?? 'Select…' }] : []),
    ...(missing ? [{ value: String(value), label: unavailableLabel ? unavailableLabel(value) : `${value} (unavailable)` }] : []),
    ...choices.map((choice) => ({ value: String(choice.value), label: choice.label, title: choice.description, disabled: choice.disabled, group: choice.group })),
  ]
  return <ListableField {...rest} value={value === undefined ? '' : String(value)} options={options}
    onChange={(next) => onChange(choices.find((choice) => String(choice.value) === next)?.value ?? (missing && next === String(value) ? value : undefined))} />
}
