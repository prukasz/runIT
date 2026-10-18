import { useState } from 'react'
import { ChoiceField, FormGrid, FormRow, NumberField, Switch, TextField, ToggleChip } from '@ui/components'
import type { Choice } from '@ui/components'
import { bundledServiceCatalog } from '@backend'
import type { BoardDescriptor, FieldInput, FieldValues, Layout, PinRef, WireField } from '@backend'
import { isReserved, noPinReady, offersMember, pinChoiceLabel, pinHint, pinNamer, pinNotes, pinReady, usedBy } from './devices'
import type { DeviceInfo } from './devices'

/*
 * The fields of a layout as a form: a device's config, a command's arguments. Each field gets the control its descriptor
 * asks for: a pin of any device (a pin reference), a pin of this device (an op's `pin`), a choice or an enum member, on / off,
 * a number in its range (several for an array), text, bytes in hex. Values are kept as the backend encodes them (by field id).
 */

/** Where pins come from: every device of the view, the one the form is about, what board.json says of the pins. */
export interface PinContext {
  readonly devices: readonly DeviceInfo[]
  readonly own?: DeviceInfo
  readonly board: BoardDescriptor
  /** The form is a command that changes the pin it is given: a pin another device manages is not offered. */
  readonly changesPin?: boolean
  /** The states a pin of the own device is in now (`pinStateOf`): a command's pin field lists only the pins in a state it wants (`pin_state`). */
  readonly pinStates?: (pin: number) => readonly string[]
}

/** "No pin" (`SYS_IO_PIN_NONE` in enums.json): its device and pin numbers, which a `device` / `pin` pair shows as None. */
const PIN_NONE = bundledServiceCatalog().pinNone

const RAW_RANGE: Partial<Record<string, readonly [number, number]>> = {
  u8: [0, 0xff], i8: [-0x80, 0x7f], u16: [0, 0xffff], i16: [-0x8000, 0x7fff], u32: [0, 0xffffffff], i32: [-0x80000000, 0x7fffffff],
}
const WIDTH: Partial<Record<string, number>> = { u8: 1, i8: 1, u16: 2, i16: 2, u32: 4, i32: 4, u64: 8, i64: 8, float: 4, double: 8 }

/** The value a field shows: the one set, else its default, else none / zero. */
/** A field the CREATE needs and nothing gives: no value, no default, not optional. The form paints it red. */
export const isMissing = (field: WireField, values: FieldValues): boolean => {
  const v = values[field.id]
  if (field.optional || (field.default !== undefined && field.default !== null)) return false
  return v === undefined || v === null || (Array.isArray(v) && v.some((x) => x === null))
}

export const fieldValue = (field: WireField, values: FieldValues): FieldInput | undefined => values[field.id] ?? (field.default as FieldInput | undefined)

/** The pins of device `d` a reference may take: free ones only (not used by another device, not reserved), and the one it holds now. */
const freePins = (context: PinContext, d: DeviceInfo, current: PinRef | null): Choice<number>[] => {
  const pins = d.type?.pins
  if (!pins) return []
  const notes = pinNotes(context.board, d.id)
  const users = usedBy(context.devices, d.id).filter((u) => u.by.id !== context.own?.id)
  return Array.from({ length: pins.count }, (_, pin) => pin)
    .filter((pin) => (current?.device === d.id && current.pin === pin) || (!users.some((u) => u.use.pin.pin === pin) && !isReserved(context.board, d.id, pin)))
    .map((pin): Choice<number> => {
      const note = notes.get(pin)
      return { value: pin, label: current?.device === d.id && current.pin === pin ? pinNamer(context.devices)(d.id, pin) : pinChoiceLabel(context.devices, d.id, pin), ...(note ? { description: note } : {}) }
    })
}

/** A pin of any device, in two parts: the device (only those with a free pin of the mode the field requires), then its pin. */
function PinRefControl({ error, disabled, label, value, requires, context, onChange }: { error: boolean; disabled: boolean; label: string; value: PinRef | null; requires: string | undefined; context: PinContext; onChange: (value: PinRef | null) => void }) {
  const [picked, setPicked] = useState<number | undefined>()
  const device = value?.device ?? picked
  const candidates = context.devices.filter((d) => d.type?.pins && d.id !== context.own?.id && (!requires || d.type.pins.modes.includes(requires)))
  const devices = candidates.filter((d) => d.id === device || freePins(context, d, value).length > 0).map((d): Choice<number> => ({ value: d.id, label: d.name }))
  const target = candidates.find((d) => d.id === device)
  return (
    <span className="fields-pinref">
      <ChoiceField<number> aria-label={`${label}: device`} value={device} choices={devices} emptyLabel="No device" placeholder="Choose a device" error={error} disabled={disabled}
        onChange={(id) => { setPicked(id); if (id === undefined || id !== value?.device) onChange(null) }} unavailableLabel={(id) => `Device ${id} (not in the project)`} />
      <ChoiceField<number> aria-label={`${label}: pin`} value={value?.pin} choices={target ? freePins(context, target, value) : []} disabled={disabled || !target} error={error} placeholder="Choose a pin"
        onChange={(pin) => device !== undefined && pin !== undefined && onChange({ device, pin })} />
    </span>
  )
}

function FieldControl({ field, value, onChange, context, missing, disabled, layout, values }: { field: WireField; value: FieldInput | undefined; onChange: (value: FieldInput) => void; context: PinContext; missing: boolean; disabled: boolean; layout: Layout; values: FieldValues }) {
  const label = `${field.label}${field.unit ? ` (${field.unit})` : ''}`
  if (field.pinRef) {
    const count = (field.size ?? 3) / 3
    const list: (PinRef | null)[] = count === 1 ? [(value as PinRef | null | undefined) ?? null] : Array.from({ length: count }, (_, i) => ((value as (PinRef | null)[] | undefined)?.[i]) ?? null)
    const set = (i: number, pin: PinRef | null) => {
      const next = list.map((p, j) => (j === i ? pin : p))
      onChange(count === 1 ? next[0]! : next)
    }
    return (
      <>
        {list.map((pin, i) => (
          <PinRefControl key={i} disabled={disabled} error={missing && !pin} label={count === 1 ? label : `${label} ${i + 1}`} value={pin} requires={field.requires} context={context} onChange={(next) => set(i, next)} />
        ))}
      </>
    )
  }
  if (field.choices) {
    const choices = field.choices.filter((c) => offersMember(context.own, field.enum, Number(c.value))).map((c): Choice<string> => ({ value: String(c.value), label: c.label, ...(c.description ? { description: c.description } : {}) }))
    return <ChoiceField<string> aria-label={label} error={missing} disabled={disabled} value={value === undefined ? undefined : String(value)} choices={choices}
      onChange={(next) => { const choice = field.choices!.find((c) => String(c.value) === next); if (choice) onChange(typeof choice.value === 'string' ? choice.label : Number(choice.value)) }} />
  }
  if (field.type === 'pin' && context.own?.type?.pins) {
    const pins = context.own.type.pins
    const own = context.own
    const users = usedBy(context.devices, own.id)
    // The pins the board keeps for itself are not offered; a command that changes the pin is offered only the pins nobody else manages.
    const choices = Array.from({ length: pins.count }, (_, pin) => pin)
      .filter((pin) => !isReserved(context.board, own.id, pin) && (!context.changesPin || !users.some((u) => u.use.pin.pin === pin)))
      // Only the pins in a state the command wants: it would be refused on any other (SET_LEVEL on a pin with no mode).
      .filter((pin) => !context.pinStates || pinReady(context.pinStates(pin), field.pinState))
      .map((pin): Choice<number> => {
        const hint = pinHint(own.type, pin)
        return { value: pin, label: pinChoiceLabel(context.devices, own.id, pin), ...(hint ? { description: hint } : {}) }
      })
    const none = choices.length === 0 && field.pinState !== undefined && context.pinStates !== undefined
    return <ChoiceField<number> aria-label={label} disabled={disabled || none} value={typeof value === 'number' && choices.some((c) => c.value === value) ? value : undefined} choices={choices}
      placeholder={none ? noPinReady(field.pinState!) : 'Choose a pin'} onChange={(pin) => pin !== undefined && onChange(pin)} />
  }
  // A device by its ID (a `device` field), one of the view's; an ID the view does not have stays shown.
  if (field.type === 'device') {
    const choices = [...(PIN_NONE ? [{ value: PIN_NONE.device, label: 'None' }] : []), ...context.devices.map((d): Choice<number> => ({ value: d.id, label: d.name }))]
    return <ChoiceField<number> aria-label={label} disabled={disabled} value={typeof value === 'number' ? value : undefined} choices={choices} placeholder="Choose a device"
      unavailableLabel={(id) => `Device ${id}`} onChange={(id) => id !== undefined && onChange(id)} />
  }
  // A pin of the device a `device` field of the same layout names (a struct that gives the two apart).
  if (field.type === 'pin') {
    const deviceField = layout.fields.find((f) => f.type === 'device')
    const target = deviceField ? context.devices.find((d) => d.id === Number(fieldValue(deviceField, values))) : undefined
    const none = PIN_NONE !== undefined && deviceField !== undefined && Number(fieldValue(deviceField, values)) === PIN_NONE.device
    const choices = none
      ? [{ value: PIN_NONE.pin, label: 'None' }]
      : target?.type?.pins
      ? Array.from({ length: target.type.pins.count }, (_, pin): Choice<number> => {
          const hint = pinHint(target.type, pin)
          return { value: pin, label: pinChoiceLabel(context.devices, target.id, pin), ...(hint ? { description: hint } : {}) }
        })
      : []
    return <ChoiceField<number> aria-label={label} disabled={disabled || (!target && !none)} value={typeof value === 'number' ? value : undefined} choices={choices} placeholder="Choose a pin"
      unavailableLabel={(pin) => `#${pin}`} onChange={(pin) => pin !== undefined && onChange(pin)} />
  }
  // A bitmask with named bits: one chip per named bit, on or off; the bits the descriptor does not name are not offered.
  if (field.bits) {
    const mask = Number(value ?? 0)
    return (
      <span className="fields-bits">
        {field.bits.map((b) => {
          const on = ((mask >> b.bit) & 1) === 1
          return <ToggleChip key={b.bit} selected={on} disabled={disabled} onClick={() => onChange(mask ^ (1 << b.bit))}>{b.label}</ToggleChip>
        })}
      </span>
    )
  }
  if (field.type === 'boolean') return <Switch label={value ? 'On' : 'Off'} checked={Boolean(value)} onChange={onChange} disabled={disabled} />
  if (field.rawType === 'string') return <TextField aria-label={label} disabled={disabled} value={typeof value === 'string' ? value : ''} onChange={(event) => onChange(event.target.value)} />
  if (field.rawType === 'bytes') {
    const bytes = Array.isArray(value) ? (value as number[]) : []
    return <TextField aria-label={`${label} (hex)`} disabled={disabled} placeholder="00 01 ff" defaultValue={bytes.map((b) => b.toString(16).padStart(2, '0')).join(' ')}
      onChange={(event) => { const parts = event.target.value.trim().split(/[\s,]+/).filter(Boolean).map((h) => parseInt(h, 16)); if (parts.every((b) => b >= 0 && b <= 0xff)) onChange(parts) }} />
  }
  const [lo, hi] = RAW_RANGE[field.rawType] ?? [undefined, undefined]
  const min = field.min ?? lo
  const max = field.max ?? hi
  const integer = field.rawType !== 'float' && field.rawType !== 'double'
  const width = WIDTH[field.rawType] ?? 1
  const count = field.size !== undefined ? field.size / width : 1
  if (count > 1) {
    const list = Array.from({ length: count }, (_, i) => Number((value as number[] | undefined)?.[i] ?? 0))
    return (
      <span className="fields-array">
        {list.map((v, i) => <NumberField key={i} disabled={disabled} aria-label={`${label} ${i + 1}`} value={v} min={min} max={max} integer={integer} onCommit={(n) => onChange(list.map((o, j) => (j === i ? n : o)))} />)}
      </span>
    )
  }
  return <NumberField aria-label={label} disabled={disabled} value={Number(value ?? 0)} min={min} max={max} integer={integer} onCommit={onChange} />
}

/** A form of a layout's fields; `onChange` gets every value set so far with the changed one. `disabled` shows them locked (a board device's own settings). */
export function FieldsForm({ layout, values, onChange, context, hide, disabled = false, grid = true }: { layout: Layout; values: FieldValues; onChange: (values: FieldValues) => void; context: PinContext; hide?: (field: WireField) => boolean; disabled?: boolean; grid?: boolean }) {
  const rows = layout.fields.filter((field) => !hide?.(field)).map((field) => (
    <FormRow key={field.id} className={field.pinRef ? 'is-wide' : ''} label={`${field.label}${field.unit ? ` (${field.unit})` : ''}`} note={field.description}>
      <FieldControl field={field} value={fieldValue(field, values)} context={context} missing={isMissing(field, values)} disabled={disabled} layout={layout} values={values} onChange={(value) => onChange({ ...values, [field.id]: value })} />
    </FormRow>
  ))
  return grid ? <FormGrid>{rows}</FormGrid> : <>{rows}</>
}
