import { DEVICE_STATE } from '../../catalog/layouts'
import type { BlockType, Field, Variant } from '../../catalog/types'
import type { Diagnostic } from '../../model/program/diagnostics'
import type { ConcretePins } from '../../model/program/pins'
import type { BlockInstance } from '../../model/program/program'
import { ID_NONE } from '../../wire/format/types'
import type { CodecRegistry } from './codecs'
import { floatBits, RAW, wireNumber, writeRaw } from './values'

/*
 * A block's state: the bytes after its pins in the ADD_BLOCK record (`custom_data`, `custom_len` long).
 *
 *   core block      the fields of the descriptor at their offsets; a formula or cases by the block's codec; a loop's span
 *   device block    `vm_block_device_data_t` (56 bytes): the device id, the operation, the idle operation, and the constant of
 *                   each input of the operation for while the input is unwired (`k[]`)
 */

export interface StateContext {
  readonly type: BlockType
  readonly instance: BlockInstance
  readonly pins: ConcretePins
  readonly variant: Variant | undefined
  /** Positions in the execution order of the run a loop block owns. */
  readonly span?: { readonly start: number; readonly end: number }
  readonly codecs: CodecRegistry
  readonly report: (severity: Diagnostic['severity'], code: string, message: string, extra?: Partial<Diagnostic['where']>) => void
}

/** Inputs the user wired, by field id: a wired field does not need its own value. */
export type WiredFields = ReadonlySet<string>

const encodeFields = (bytes: Uint8Array, fields: readonly Field[], ctx: StateContext, wired: WiredFields): void => {
  for (const field of fields) {
    const inState = field.offset !== undefined && field.rawType !== undefined && field.rawType !== 'bytes'
    if (!inState) continue
    const given = ctx.instance.values?.[field.id]
    if (given === undefined && field.default === undefined && field.fixed === undefined) {
      if (field.required && !wired.has(field.id)) ctx.report('error', 'field-missing', `'${field.alias}' needs a value.`, { field: field.id })
      continue
    }
    const result = wireNumber(field, given)
    if (!result.ok) {
      ctx.report('error', 'field-invalid', result.message, { field: field.id })
      continue
    }
    const raw = field.rawType as Exclude<NonNullable<Field['rawType']>, 'bytes'>
    const offset = field.offset as number
    if (offset + RAW[raw].width > bytes.length) {
      ctx.report('error', 'field-outside-state', `'${field.alias}' lies outside the ${bytes.length} bytes of the block's state.`, { field: field.id })
      continue
    }
    writeRaw(bytes, offset, raw, result.number)
  }
}

/** `vm_block_device_data_t`: device id, op, idle op, the side of op, then `k[]` for the operation's input fields in the command's order. */
const encodeDevice = (ctx: StateContext, wired: WiredFields): Uint8Array | null => {
  const { type, instance, variant } = ctx
  if (!variant) {
    ctx.report('error', 'variant-missing', `'${type.alias}' needs one of its operations.`)
    return null
  }
  if (variant.unsupported) {
    ctx.report('error', 'variant-unsupported', `'${variant.alias}' cannot be compiled yet: ${variant.unsupported}.`)
    return null
  }
  const operation = variant.operation
  if (!operation) {
    ctx.report('error', 'variant-unsupported', `'${variant.alias}' has no operation.`)
    return null
  }
  if (instance.device === undefined || instance.device === null || instance.device === ID_NONE) {
    ctx.report('error', 'device-missing', `'${type.alias}' needs a device: choose which one it works on.`)
    return null
  }
  if (!Number.isInteger(instance.device) || instance.device < 0 || instance.device > 0xfffe) {
    ctx.report('error', 'device-invalid', `${instance.device} is not a device id.`)
    return null
  }
  if (variant.fields.length > DEVICE_STATE.fields) {
    ctx.report('error', 'too-many-arguments', `'${variant.alias}' takes ${variant.fields.length} arguments; a device block carries ${DEVICE_STATE.fields}.`)
    return null
  }

  const bytes = new Uint8Array(type.stateLength)
  writeRaw(bytes, DEVICE_STATE.deviceId, 'u16', instance.device)
  writeRaw(bytes, DEVICE_STATE.op, 'u16', operation.value)
  writeRaw(bytes, DEVICE_STATE.dir, 'u8', operation.dir)
  // block-level fields with an offset: `idle` is the idle operation, written at its place in the state
  encodeFields(bytes, type.fields, ctx, wired)

  variant.fields.forEach((field, slot) => {
    const given = instance.values?.[field.id]
    if (given === undefined && field.default === undefined && field.fixed === undefined) {
      // no constant: fine while a wire supplies the value (`k` stays zero), a mistake otherwise when the argument is required
      if (field.required && !wired.has(field.id)) ctx.report('error', 'field-missing', `'${field.alias}' needs a value or a wire.`, { field: field.id })
      return
    }
    const result = wireNumber({ ...field, scale: undefined, rawType: undefined }, given)
    if (!result.ok) {
      ctx.report('error', 'field-invalid', result.message, { field: field.id })
      return
    }
    // A scaled argument keeps its constant as a float in the block's own unit; the board multiplies it on the way out.
    const scaled = field.scale !== undefined && field.scale !== 0 && field.scale !== 1
    writeRaw(bytes, DEVICE_STATE.k + slot * 4, 'i32', scaled ? floatBits(result.number) : Math.round(result.number))
  })
  return bytes
}

const encodeCore = (ctx: StateContext, wired: WiredFields): Uint8Array | null => {
  const { type, instance } = ctx
  if (type.stateVariable) {
    const codec = type.editor ? ctx.codecs.get(type.editor) : undefined
    if (!codec) {
      ctx.report('error', 'codec-missing', `No codec for the '${type.editor ?? type.id}' editor: the block's state cannot be built.`)
      return null
    }
    return codec.encode({
      type,
      instance,
      pins: ctx.pins,
      report: (severity, code, message) => ctx.report(severity, code, message),
    })
  }
  const bytes = new Uint8Array(type.stateLength)
  encodeFields(bytes, [...type.fields, ...(ctx.variant?.fields ?? [])], ctx, wired)
  if (type.span && ctx.span) {
    if (type.span.offset + 4 > bytes.length) ctx.report('error', 'field-outside-state', "The loop's span lies outside the block's state.")
    else {
      writeRaw(bytes, type.span.offset, 'u16', ctx.span.start)
      writeRaw(bytes, type.span.offset + 2, 'u16', ctx.span.end)
    }
  }
  return bytes
}

/** The block's state bytes, or null (with the reason reported) when they cannot be made. */
export const encodeState = (ctx: StateContext, wired: WiredFields): Uint8Array | null =>
  ctx.type.integration === 'core' ? encodeCore(ctx, wired) : encodeDevice(ctx, wired)
