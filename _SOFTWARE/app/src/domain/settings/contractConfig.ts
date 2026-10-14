import contractsJson from '@data-structures/contracts/contracts.generated.json'
import { enumMembers } from '../../backend/catalog'

/*
 * A router op whose request is one settings struct (`sys_log_cfg_t`, `sys_power_manager_cfg_t`), read from the generated contracts:
 * the form is built from the fields' aliases, descriptions, choices, limits, units and defaults, so a field added to the C struct
 * (with its annotations) shows up here after the descriptors are regenerated, and nothing about it is written in the app.
 */

interface RawField {
  readonly name: string
  readonly offset: number
  readonly type: string
  readonly size: number
  readonly alias: string
  readonly description?: string
  readonly choices?: readonly { readonly value: number; readonly alias: string }[]
  readonly min?: number
  readonly max?: number
  readonly unit?: string
  readonly default?: string
  readonly enum_ref?: string
  readonly struct?: string
  readonly hidden?: boolean
}

interface RawOp {
  readonly id: number
  readonly symbol: string
  readonly alias: string
  readonly description: string
  readonly settings?: boolean
  readonly record?: { readonly symbol: string; readonly id: number }
  readonly args?: { readonly struct: string; readonly size: number; readonly fields: readonly RawField[] }
}

interface RawRouter {
  readonly key: number
  readonly symbol: string
  readonly alias: string
  readonly description: string
  readonly ops?: readonly RawOp[]
}

interface RawTable {
  readonly name: string
  readonly alias: string
  readonly description: string
  readonly x: { readonly alias: string; readonly unit: string }
  readonly y: { readonly alias: string; readonly unit: string }
  readonly for: string
  readonly points: readonly (readonly [number, number])[]
}

const FILE = contractsJson as unknown as { routers: RawRouter[]; tables?: RawTable[]; device_contracts: { name: string; ops: RawDeviceOp[] }[] }
const ROUTERS = FILE.routers

/** One number of the struct, as the form shows it. */
export interface ConfigField {
  readonly name: string
  readonly alias: string
  readonly description?: string
  readonly size: number
  readonly signed: boolean
  readonly min?: number
  readonly max?: number
  readonly unit?: string
  /** Decided by the board (`@hidden` in C): sent with its default, not shown in the form. */
  readonly hidden?: boolean
  /** The firmware's own start value (`@default` in the C struct). */
  readonly default?: number
  readonly choices?: readonly { readonly value: number; readonly alias: string; /** The C name of the member of a published enum the choice is. */ readonly symbol?: string }[]
}

export interface ConfigOp {
  readonly routerKey: number
  /** The router's C name (`ROUTER_KEY_POWER`). */
  readonly routerSymbol: string
  readonly routerAlias: string
  readonly opId: number
  /** The op's C name (`SYS_PWRM_OP_SET_BATTERY`). */
  readonly symbol: string
  readonly alias: string
  readonly description: string
  readonly struct: string
  /** Bytes of the packed struct. */
  readonly size: number
  /** The numbers of the struct, in wire order. */
  readonly fields: readonly ConfigField[]
  /** Fields that are not one number (a pin of a device): the form leaves them to the board. */
  readonly skipped: readonly string[]
  /** Every field of the struct, with its offset, for building the frame. */
  readonly layout: readonly { readonly name: string; readonly offset: number; readonly size: number }[]
}

/** The choices of a field: its `@choices`, or the members of the published enum it names (`@enum-ref`). */
const choicesOf = (raw: RawField): ConfigField['choices'] => {
  if (raw.choices?.length) return raw.choices
  if (raw.enum_ref) return enumMembers(raw.enum_ref).map((member) => ({ value: member.value, alias: member.alias, symbol: member.name }))
  return undefined
}

const numeric = (raw: RawField): boolean => /^[ui](8|16|32)$/.test(raw.type)

const toConfigOp = (router: RawRouter, op: RawOp): ConfigOp => {
  const args = op.args ?? { struct: '', size: 0, fields: [] }
  const fields = args.fields.filter(numeric).map((raw): ConfigField => {
    const parsed = raw.default === undefined ? undefined : Number(raw.default)
    return {
      name: raw.name,
      alias: raw.alias,
      ...(raw.description ? { description: raw.description } : {}),
      size: raw.size,
      signed: raw.type.startsWith('i'),
      ...(raw.min !== undefined ? { min: raw.min } : {}),
      ...(raw.max !== undefined ? { max: raw.max } : {}),
      ...(raw.unit ? { unit: raw.unit } : {}),
      ...(raw.hidden ? { hidden: true } : {}),
      ...(parsed !== undefined && Number.isFinite(parsed) ? { default: parsed } : {}),
      ...(choicesOf(raw) ? { choices: choicesOf(raw)! } : {}),
    }
  })
  return {
    routerKey: router.key,
    routerSymbol: router.symbol,
    routerAlias: router.alias,
    opId: op.id,
    symbol: op.symbol,
    alias: op.alias,
    description: op.description,
    struct: args.struct,
    size: args.size,
    fields,
    skipped: args.fields.filter((raw) => !numeric(raw) && !raw.hidden).map((raw) => raw.alias),
    layout: args.fields.map((raw) => ({ name: raw.name, offset: raw.offset, size: raw.size })),
  }
}

/** The op `opSymbol` of router `routerSymbol` as a settings form; throws a message naming it when the descriptors lack it. */
export const configOp = (routerSymbol: string, opSymbol: string): ConfigOp => {
  const router = ROUTERS.find((entry) => entry.symbol === routerSymbol)
  const op = router?.ops?.find((entry) => entry.symbol === opSymbol)
  if (!router || !op?.args) throw new Error(`The firmware publishes no settings op '${opSymbol}' on '${routerSymbol}' (data-structures/contracts/contracts.generated.json).`)
  return toConfigOp(router, op)
}

/** What a device command is as the contracts publish it (a contract's op, or an op a driver has of its own). */
export interface RawDeviceOp {
  readonly symbol: string
  readonly id: number
  readonly alias: string
  readonly description?: string
  readonly args?: RawOp['args']
  /** What the command answers with: a command that reads is not one a default setting or an action holds. */
  readonly returns?: unknown
}

/** The op `symbol` of device contract `contract` (`IO`), as the contracts publish it. */
export const deviceContractOp = (contract: string, symbol: string): RawDeviceOp | undefined =>
  FILE.device_contracts.find((entry) => entry.name === contract)?.ops.find((op) => op.symbol === symbol)

/** A device command as a form: its arguments are packed and read back like any settings struct (no arguments: an empty struct). */
export const deviceOpConfig = (op: RawDeviceOp): ConfigOp => {
  const router = ROUTERS.find((entry) => entry.symbol === 'ROUTER_KEY_DEVICE')
  if (!router) throw new Error("The firmware publishes no router 'ROUTER_KEY_DEVICE' (data-structures/contracts/contracts.generated.json).")
  return toConfigOp(router, { description: '', ...op })
}

/** Every op of a router tagged `@settings` in C, in the order the firmware lists them: the router's settings page is these forms. */
export const settingsOps = (routerSymbol: string): ConfigOp[] => {
  const router = ROUTERS.find((entry) => entry.symbol === routerSymbol)
  if (!router) throw new Error(`The firmware publishes no router '${routerSymbol}' (data-structures/contracts/contracts.generated.json).`)
  return (router.ops ?? []).filter((op) => op.settings && op.args).map((op) => toConfigOp(router, op))
}

/** The id of the record the firmware keeps an op's frame in (`@record` in C: `SYS_ACTIONS_ID_LOG`), if it keeps one. */
export const opRecord = (routerSymbol: string, opSymbol: string): number | undefined =>
  ROUTERS.find((entry) => entry.symbol === routerSymbol)?.ops?.find((entry) => entry.symbol === opSymbol)?.record?.id

/** Field name → value. */
export type ConfigValues = Readonly<Record<string, number>>

/** The firmware's start values; a field with no `@default` starts at its lowest allowed value (or 0). */
export const defaultConfig = (op: ConfigOp): ConfigValues =>
  Object.fromEntries(op.fields.map((field) => [field.name, field.default ?? field.min ?? field.choices?.[0]?.value ?? 0]))

/** Whether a value is one the field allows: a listed choice, or inside its limits and its size. */
export const configValueOk = (field: ConfigField, value: unknown): value is number => {
  if (typeof value !== 'number' || !Number.isInteger(value)) return false
  if (field.choices) return field.choices.some((choice) => choice.value === value)
  const bits = field.size * 8
  const low = field.signed ? -(2 ** (bits - 1)) : 0
  const high = field.signed ? 2 ** (bits - 1) - 1 : 2 ** bits - 1
  return value >= Math.max(low, field.min ?? low) && value <= Math.min(high, field.max ?? high)
}

/** A saved copy over the defaults: a field the copy lacks or holds a wrong value for takes its default, unknown fields are dropped. */
export const configFromJson = (op: ConfigOp, raw: string): ConfigValues | undefined => {
  const value: unknown = JSON.parse(raw)
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return undefined
  const saved = value as Record<string, unknown>
  const defaults = defaultConfig(op)
  return Object.fromEntries(op.fields.map((field) => [field.name, configValueOk(field, saved[field.name]) ? (saved[field.name] as number) : defaults[field.name]!]))
}

/** The request payload of the op: the struct packed little-endian, the fields the form does not hold left at 0. */
export const encodeConfig = (op: ConfigOp, values: ConfigValues): Uint8Array => {
  const bytes = new Uint8Array(op.size)
  for (const slot of op.layout) {
    const value = values[slot.name]
    if (value === undefined) continue
    for (let index = 0; index < slot.size; index++) bytes[slot.offset + index] = Math.floor(value / 2 ** (8 * index)) & 0xff
  }
  return bytes
}

/** The numbers of a request payload (what `encodeConfig` packs) back into field values; a field the payload is too short for is left out. */
export const decodeConfig = (op: ConfigOp, bytes: Uint8Array): ConfigValues => {
  const values: Record<string, number> = {}
  for (const field of op.fields) {
    const slot = op.layout.find((entry) => entry.name === field.name)
    if (!slot || slot.offset + slot.size > bytes.length) continue
    let value = 0
    for (let index = slot.size - 1; index >= 0; index--) value = value * 256 + bytes[slot.offset + index]!
    values[field.name] = field.signed && value >= 2 ** (slot.size * 8 - 1) ? value - 2 ** (slot.size * 8) : value
  }
  return values
}

/** A curve the firmware publishes (`//#table` in C): points of x → y for one member of an enum, e.g. a battery's charge by cell voltage. */
export interface ConfigCurve {
  readonly name: string
  readonly alias: string
  readonly description: string
  readonly x: { readonly alias: string; readonly unit: string }
  readonly y: { readonly alias: string; readonly unit: string }
  /** The enum member (C name) the curve belongs to. */
  readonly forSymbol: string
  readonly points: readonly (readonly [number, number])[]
}

/** The curve published for an enum member (`SYS_POWER_BATTERY_LIPO`), if any. */
export const curveFor = (symbol: string | undefined): ConfigCurve | undefined => {
  const table = symbol ? FILE.tables?.find((entry) => entry.for === symbol) : undefined
  return table && { name: table.name, alias: table.alias, description: table.description, x: table.x, y: table.y, forSymbol: table.for, points: table.points }
}

/** x at which the curve reaches `y` (linear between points); undefined outside the curve. */
export const curveXAt = (curve: ConfigCurve, y: number): number | undefined => {
  for (let index = 1; index < curve.points.length; index++) {
    const [x0, y0] = curve.points[index - 1]!
    const [x1, y1] = curve.points[index]!
    if ((y >= y0 && y <= y1) || (y >= y1 && y <= y0)) return y1 === y0 ? x0 : x0 + ((y - y0) * (x1 - x0)) / (y1 - y0)
  }
  return undefined
}
