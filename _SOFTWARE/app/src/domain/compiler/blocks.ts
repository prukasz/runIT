import { blockPinAt } from '../descriptors'
import type { VmBlockField, VmBlockType, VmCatalog } from '../descriptors'
import type { ObjectPath, ObjectSection, ProgramBlock, ValueNode } from '../project'
import { isDynamicInput, readPinMask } from '../project/blockPins'
import type { AccessorLayout, AccessorRequest, PathReach } from './accessors'
import type { Diagnostic, ObjectLayout } from './objects'

/*
 * Program blocks → 0x45 records, from the block palette (vm/blocks).
 *
 * Two stages around the object and accessor stages:
 * - planBlocks: block-owned objects (outputs `<id>:q<n>`, ENO `<id>:eno`) as
 *   an object section, the user objects blocks drive, and one accessor
 *   request per wired input (`block:<id>:in<n>`) and enable
 *   (`block:<id>:en<n>`); pin counts and one writer per object.
 * - encodeBlocks: with wire IDs known, pin kinds against what each path
 *   reaches, the private state from the settings (enum members, ranges, the
 *   FOR span, the expression bytecode), the block type's own load rules, and
 *   the records in execution order (block ID = position).
 */

export const BLOCK_SECTION = 'blocks'
export const outputObjectId = (blockId: string, pin: number): string => `${blockId}:q${pin}`
export const enoObjectId = (blockId: string): string => `${blockId}:eno`
export const inputKey = (blockId: string, pin: number): string => `block:${blockId}:in${pin}`
export const enableKey = (blockId: string, index: number): string => `block:${blockId}:en${index}`

/** Object type of a block-owned output, by its pin's value kind. */
const OUTPUT_TYPES: Readonly<Record<string, string>> = { gate: 'B', bool: 'B', u8: 'U8', u32: 'U32', i32: 'I32', f32: 'F' }
const SCALAR_KINDS = new Set(['bool', 'u8', 'u32', 'i32', 'f32', 'scalar', 'gate'])

interface PlannedBlock {
  readonly block: ProgramBlock
  readonly type: VmBlockType
  /** Request key per input pin, null = unwired. */
  readonly inputs: readonly (string | null)[]
  /** Object project ID per output pin. */
  readonly outputs: readonly string[]
  readonly enables: readonly string[]
  readonly eno?: string
}

export interface BlockPlan {
  /** Block-owned objects, compiled after the user's tree. */
  readonly section: ObjectSection
  /** User objects a block writes. */
  readonly driven: ReadonlySet<string>
  readonly requests: readonly AccessorRequest[]
  readonly diagnostics: readonly Diagnostic[]
  readonly blocks: readonly PlannedBlock[]
}

export const planBlocks = (blocks: readonly ProgramBlock[], catalog: VmCatalog): BlockPlan => {
  const diagnostics: Diagnostic[] = []
  const owned: ValueNode[] = []
  const driven = new Set<string>()
  const writerOf = new Map<string, string>()
  const requests: AccessorRequest[] = []
  const planned: PlannedBlock[] = []
  const ids = new Set<string>()
  // ENO is always a source in the editor; materialize only referenced outputs.
  const referenced = new Set<string>()
  const visitPath = (path: ObjectPath) => {
    referenced.add(path.root)
    for (const step of path.steps ?? []) if (step.kind === 'dynamic') visitPath(step.index)
  }
  for (const block of blocks) {
    for (const path of [...(block.inputs ?? []), ...(block.enables ?? [])]) if (path) visitPath(path)
  }

  for (const block of blocks) {
    const error = (message: string, firmwareError?: string) => diagnostics.push({ severity: 'error', blockId: block.id, message: `Block '${block.id}': ${message}`, ...(firmwareError ? { firmwareError } : {}) })
    if (ids.has(block.id)) error('the ID is used twice.')
    ids.add(block.id)
    const type = catalog.block(block.type)
    if (!type) {
      error(`unknown block type '${block.type}' (one of ${catalog.blocks.map((entry) => entry.key).join(', ')}).`, 'ERR_VM_BLK_UNKNOWN_TYPE')
      continue
    }

    const wired = block.inputs ?? []
    const inCount = Math.max(wired.length, type.inputs.min)
    if (inCount > Math.min(type.inputs.max, catalog.blockPinMax.in)) error(`${inCount} inputs, ${type.title} takes at most ${type.inputs.max}.`, 'ERR_VM_BLK_BAD_SHAPE')
    const inputs = Array.from({ length: inCount }, (_, pin): string | null => {
      const path = wired[pin]
      const descriptor = blockPinAt(type.inputs, pin)
      if (!path) {
        if (descriptor?.required) error(`input '${descriptor.title}' must be wired.`, 'ERR_VM_BLK_BAD_SHAPE')
        return null
      }
      const key = inputKey(block.id, pin)
      requests.push({ key, path })
      return key
    })

    const chosen = block.outputs ?? []
    // Every listed output pin gets an object (other blocks may read it); repeated ones (SWITCH) as many as listed.
    const outCount = Math.max(chosen.length, type.outputs.min, type.outputs.pins.length)
    if (outCount > Math.min(type.outputs.max, catalog.blockPinMax.out)) error(`${outCount} outputs, ${type.title} has at most ${type.outputs.max}.`, 'ERR_VM_BLK_BAD_SHAPE')
    const claim = (objectId: string) => {
      const other = writerOf.get(objectId)
      if (other !== undefined) error(`'${objectId}' is already written by block '${other}': one writer per object.`, 'ERR_VM_BLK_OUTPUT_TAKEN')
      else writerOf.set(objectId, block.id)
    }
    const outputs = Array.from({ length: outCount }, (_, pin): string => {
      const target = chosen[pin]
      if (target) {
        claim(target)
        driven.add(target)
        return target
      }
      const kind = blockPinAt(type.outputs, pin)?.value ?? 'f32'
      const id = outputObjectId(block.id, pin)
      owned.push({ kind: 'value', id, name: '', type: OUTPUT_TYPES[kind] ?? 'F', length: 1, mutable: true, retentive: false })
      return id
    })

    const enableSources = block.enables ?? []
    if (enableSources.length > catalog.blockPinMax.en) error(`${enableSources.length} enables, at most ${catalog.blockPinMax.en}.`, 'ERR_VM_BLK_BAD_SHAPE')
    const enables = enableSources.map((path, index) => {
      const key = enableKey(block.id, index)
      requests.push({ key, path })
      return key
    })

    let eno: string | undefined
    if (block.eno || referenced.has(enoObjectId(block.id))) {
      eno = enoObjectId(block.id)
      owned.push({ kind: 'value', id: eno, name: '', type: 'B', length: 1, mutable: true, retentive: false })
    }
    planned.push({ block, type, inputs, outputs, enables, ...(eno ? { eno } : {}) })
  }

  return { section: { key: BLOCK_SECTION, owner: 'block', objects: owned }, driven, requests, diagnostics, blocks: planned }
}

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------

const INTEGER_RANGES: Readonly<Record<string, readonly [number, number]>> = {
  uint8_t: [0, 0xff],
  uint16_t: [0, 0xffff],
  uint32_t: [0, 0xffffffff],
  int32_t: [-0x80000000, 0x7fffffff],
  uint64_t: [0, Number.MAX_SAFE_INTEGER],
}

/** The expression blocks' header fields, computed from the expression. */
const EXPRESSION_COUNTS = new Set(['const_cnt', 'code_len'])

/** EXPR / EXPR_BIT constants and code bytes, checked like the device's verify. */
const encodeExpression = (type: VmBlockType, expression: ProgramBlock['expression'], inCount: number): { bytes?: Uint8Array; constCount: number; codeLength: number; problem?: string } => {
  const encoding = type.encoding!
  const constants = expression?.constants ?? []
  const code: number[] = []
  const tokens = expression?.code ?? []
  let depth = 0
  let ended = false
  const fail = (problem: string) => ({ constCount: 0, codeLength: 0, problem })
  if (!tokens.length) return fail('the expression has no code.')
  if (constants.length > 0xff) return fail(`${constants.length} constants, at most 255.`)
  for (let at = 0; at < tokens.length; at++) {
    const token = tokens[at]!
    const opcode = typeof token === 'string' ? encoding.opcode(token) : undefined
    if (!opcode) return fail(`'${token}' (position ${at + 1}) is not an opcode.`)
    code.push(opcode.value)
    if (opcode.operand !== 'none') {
      const operand = tokens[++at]
      if (typeof operand !== 'number' || !Number.isInteger(operand) || operand < 0 || operand > 0xff) return fail(`'${token}' needs a number 0..255 after it.`)
      if (opcode.operand === 'input' && operand >= inCount) return fail(`'${token} ${operand}': the block has ${inCount} input(s).`)
      if (opcode.operand === 'const' && operand >= constants.length) return fail(`'${token} ${operand}': the expression has ${constants.length} constant(s).`)
      code.push(operand)
    }
    if (ended) continue
    if (opcode.pops === 0 && opcode.pushes === 0) {
      ended = true // END: the device ignores the rest
      continue
    }
    if (depth < opcode.pops) return fail(`'${token}' (position ${at + 1}) takes ${opcode.pops} value(s), the stack holds ${depth}.`)
    depth += opcode.pushes - opcode.pops
    if (depth > encoding.stackMax) return fail(`the stack grows past ${encoding.stackMax} values.`)
  }
  if (depth !== 1) return fail(`the code leaves ${depth} values on the stack, it must leave exactly one.`)
  if (code.length > 0xffff) return fail('the code is too long.')
  const bytes = new Uint8Array(constants.length * 4 + code.length)
  const view = new DataView(bytes.buffer)
  for (const [index, constant] of constants.entries()) {
    if (encoding.constantType === 'f32') view.setFloat32(index * 4, constant, true)
    else if (Number.isInteger(constant) && constant >= 0 && constant <= 0xffffffff) view.setUint32(index * 4, constant, true)
    else return fail(`constant ${index} = ${constant} is not a whole number 0..${0xffffffff}.`)
  }
  bytes.set(code, constants.length * 4)
  return { bytes, constCount: constants.length, codeLength: code.length }
}

/** A setting as a number: an enum member by name (full or without its prefix) or a number. */
const settingValue = (type: VmBlockType, field: VmBlockField, raw: number | string): number | string => {
  if (!field.enumRef) return typeof raw === 'number' ? raw : `'${raw}' is not a number`
  const members = type.enums.get(field.enumRef) ?? []
  const member = typeof raw === 'number' ? members.find((entry) => entry.value === raw) : members.find((entry) => entry.name === raw || entry.name.endsWith(`_${raw}`))
  return member ? member.value : `${JSON.stringify(raw)} is not one of ${members.map((entry) => entry.name).join(', ')}`
}

/** A 4-byte union (EDGE's threshold) holds the type of what input 0 reads: float, signed or unsigned. */
const unionFormat = (reach: PathReach | undefined, value: number): 'float' | 'int32' | 'uint32' => {
  const key = reach?.object?.type.key
  if (key === 'F') return 'float'
  if (key === 'I32') return 'int32'
  if (key && key !== 'PTR') return 'uint32'
  return !Number.isInteger(value) ? 'float' : value < 0 ? 'int32' : 'uint32'
}

/**
 * Load rules of single block types (their descriptors' `rules`, which are
 * prose): a fallback setting the block needs while an input is unwired, and
 * the pin masks of the IO blocks.
 */
const TYPE_RULES: Readonly<Record<string, (wired: (pin: number) => boolean, setting: (name: string) => number) => string | undefined>> = {
  PERIODIC: (wired, setting) => (!wired(0) && setting('period') === 0 ? 'with the period input unwired, set a period above 0.' : undefined),
  ACTION: (wired, setting) => (!wired(0) && setting('action_id') === 0 ? 'with the id input unwired, set an action_id above 0.' : undefined),
  LATCH: (wired) => (!wired(0) && !wired(1) ? 'wire set, reset or both.' : undefined),
}

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

export interface PlacedBlock {
  /** Block ID = execution position. */
  readonly wireId: number
  readonly id: string
  readonly type: string
  /** Accessor wire IDs of the inputs (blockNoId = unwired) and enables; object wire IDs of the outputs. */
  readonly inputs: readonly number[]
  readonly outputs: readonly number[]
  readonly enables: readonly number[]
  readonly eno?: number
  /** FOR: the blocks [start, end) it runs. */
  readonly span?: { readonly start: number; readonly end: number }
}

export interface BlockLayout {
  /** Index = wire ID. */
  readonly blocks: readonly PlacedBlock[]
  /** Block project ID → wire ID. */
  readonly wireIdOf: ReadonlyMap<string, number>
}

export interface CompiledBlocks {
  readonly layout: BlockLayout
  readonly diagnostics: readonly Diagnostic[]
  /** 0x45 records in execution order, one per frame. */
  readonly records: readonly Uint8Array[]
  /** Arena bytes of the blocks and their registry. */
  readonly arenaBytes: number
}

/** Blocks that keep one state over time: a loop body runs N times in one pass, so they cannot serve each turn. */
const NOT_IN_LOOP = new Set(['TIMER', 'PERIODIC'])
/** Blocks that work in a loop but act once per turn, which is rarely what a first-time user expects. */
const ONCE_PER_TURN = new Set(['EDGE', 'LATCH', 'IO_TOGGLE', 'ACTION', 'ON_EVENT'])

const align = (size: number, to: number): number => Math.ceil(size / to) * to

export const encodeBlocks = (plan: BlockPlan, objects: ObjectLayout, accessors: AccessorLayout, catalog: VmCatalog): CompiledBlocks => {
  const diagnostics: Diagnostic[] = []
  const records: Uint8Array[] = []
  const placed: PlacedBlock[] = []
  /** Open loop spans: the block ID each ends at. */
  const loops: { end: number; id: string }[] = []
  const count = plan.blocks.length
  let arenaBytes = count ? align(count * catalog.arena.pointer, catalog.arena.alignment) : 0

  for (const [wireId, { block, type, inputs, outputs, enables, eno }] of plan.blocks.entries()) {
    const error = (message: string, firmwareError?: string) => diagnostics.push({ severity: 'error', blockId: block.id, message: `Block '${block.id}' (${type.title}): ${message}`, ...(firmwareError ? { firmwareError } : {}) })

    // Pin kinds against what the paths reach.
    const checkPin = (key: string, kind: string, what: string) => {
      const reach = accessors.reachOf.get(key)
      if (!reach?.object) return
      const folder = reach.object.type.key === 'PTR'
      if (SCALAR_KINDS.has(kind) && folder) error(`${what} reads ${reach.element ? 'a folder entry' : 'a folder'}; it needs a value.`, 'ERR_VM_ACCESSOR_TYPE_MISMATCH')
      if (kind === 'ptr-cell' && !(folder && reach.element)) error(`${what} must be a folder entry (the slot the block fills).`)
    }
    inputs.forEach((key, pin) => key && checkPin(key, blockPinAt(type.inputs, pin)?.value ?? 'object', `input '${blockPinAt(type.inputs, pin)?.title ?? pin}'`))
    enables.forEach((key, index) => checkPin(key, 'bool', `enable ${index + 1}`))

    const objectIds = outputs.map((id) => {
      const wire = objects.wireIdOf.get(id)
      const node = wire === undefined ? undefined : objects.objects[wire]?.node
      if (wire === undefined || node?.kind === 'folder') error(`output '${id}' must be an uploaded value.`, 'ERR_VM_BLK_BAD_REF')
      return wire ?? catalog.blockNoId
    })
    const accessorIds = (keys: readonly (string | null)[]) => keys.map((key) => (key === null ? catalog.blockNoId : accessors.wireIdOf.get(key) ?? catalog.blockNoId))
    const inputIds = accessorIds(inputs)
    const enableIds = accessorIds(enables)
    const enoId = eno === undefined ? catalog.blockNoId : objects.wireIdOf.get(eno) ?? catalog.blockNoId

    // Loop spans: a FOR runs the `body` blocks after it, inside any loop around it.
    while (loops.length && loops.at(-1)!.end <= wireId) loops.pop()
    if (loops.length && NOT_IN_LOOP.has(type.key)) error(`it keeps one state, so it cannot run once per turn of loop '${loops.at(-1)!.id}'. Put it outside the loop.`)
    else if (loops.length && ONCE_PER_TURN.has(type.key)) diagnostics.push({ severity: 'warning', blockId: block.id, message: `Block '${block.id}' (${type.title}): it acts on every turn of loop '${loops.at(-1)!.id}', not once per pass.` })
    let span: PlacedBlock['span']
    if (type.fields.some((field) => field.source === 'derived' && field.cType === 'vm_span_t')) {
      const body = block.body ?? 0
      const end = wireId + 1 + body
      if (!Number.isInteger(body) || body < 0 || end > count) error(`a body of ${body} blocks runs past the end of the program.`)
      else if (loops.length && end > loops.at(-1)!.end) error(`its body runs past the end of loop '${loops.at(-1)!.id}' around it.`)
      else if (loops.length + 1 > catalog.spanDepthMax) error(`loops nest ${loops.length + 1} deep, the device runs at most ${catalog.spanDepthMax}.`, 'ERR_VM_EXEC_SPAN_DEPTH')
      span = { start: wireId + 1, end }
      loops.push({ end, id: block.id })
    } else if (block.body !== undefined) error('only a loop has a body.')

    // Private state.
    const fixed = Math.max(type.stateSize, type.minCustomLen)
    let tail: Uint8Array = new Uint8Array(0)
    const computed = new Map<string, number>()
    if (type.encoding) {
      const encoded = encodeExpression(type, block.expression, inputs.length)
      if (encoded.problem) error(encoded.problem, 'ERR_VM_BLK_BAD_SHAPE')
      tail = encoded.bytes ?? tail
      computed.set('const_cnt', encoded.constCount).set('code_len', encoded.codeLength)
    } else if (block.expression) error('only an expression block has an expression.')
    const custom = new Uint8Array(fixed + tail.byteLength)
    const view = new DataView(custom.buffer)
    custom.set(tail, fixed)

    const userFields = new Map(type.fields.filter((field) => field.source === 'user' && !field.flexible && !(type.encoding && EXPRESSION_COUNTS.has(field.name))).map((field) => [field.name, field]))
    for (const name of Object.keys(block.settings ?? {})) {
      if (!userFields.has(name)) error(`'${name}' is not a setting (${[...userFields.keys()].join(', ') || 'it has none'}).`)
    }
    const settings = new Map<string, number>()
    for (const index of block.dynamicInputs ?? []) {
      if (!Number.isInteger(index) || index < 0 || index >= type.inputs.max || !blockPinAt(type.inputs, index)?.hiddenByDefault) error(`dynamic input ${index} is not an optional hidden input.`, 'ERR_VM_BLK_BAD_SHAPE')
    }
    for (const field of type.fields) {
      if (field.flexible) continue
      if (field.source === 'derived') {
        if (field.cType !== 'vm_span_t') throw new Error(`${type.symbol}.${field.name}: the compiler can't derive a ${field.cType}.`)
        view.setUint16(field.offset, span?.start ?? 0, true)
        view.setUint16(field.offset + 2, span?.end ?? 0, true)
        continue
      }
      if (field.source !== 'user') continue // runtime / padding: 0 on the wire
      const raw = computed.get(field.name) ?? block.settings?.[field.name] ?? 0
      if (field.letUserSelectAvailable) {
        const pin = block.settings?.[field.letUserSelectAvailable] ?? 0
        const bits = field.size * 8
        if (typeof pin !== 'number' || !Number.isInteger(pin) || pin < 0 || pin >= bits) {
          error(`${field.letUserSelectAvailable} must be a pin number below ${bits}.`, 'ERR_VM_BLK_BAD_SHAPE')
          continue
        }
        const mask = isDynamicInput(block, field.dynamicInput!) ? readPinMask(raw) : 1n << BigInt(pin)
        if (mask === undefined || mask <= 0n || mask >= 1n << BigInt(bits) || !(mask & (1n << BigInt(pin)))) {
          error(`${field.name} must be a ${bits}-bit mask including the default pin ${pin}.`, 'ERR_VM_BLK_BAD_SHAPE')
          continue
        }
        view.setBigUint64(field.offset, mask, true)
        continue
      }
      const value = settingValue(type, field, raw)
      if (typeof value === 'string') {
        error(`${field.name}: ${value}.`, 'ERR_VM_BLK_BAD_SHAPE')
        continue
      }
      settings.set(field.name, value)
      if (field.cType === 'float') {
        if (!Number.isFinite(value)) error(`${field.name} = ${value} is not a number.`)
        view.setFloat32(field.offset, value, true)
        continue
      }
      if (field.cType.endsWith('_u') && field.size === 4) {
        const format = unionFormat(inputs[0] ? accessors.reachOf.get(inputs[0]) : undefined, value)
        if (format === 'float') view.setFloat32(field.offset, value, true)
        else if (format === 'int32') view.setInt32(field.offset, value, true)
        else view.setUint32(field.offset, value, true)
        continue
      }
      const range = INTEGER_RANGES[field.cType]
      if (!range) throw new Error(`${type.symbol}.${field.name}: the compiler can't write a ${field.cType}.`)
      if (!Number.isInteger(value) || value < range[0] || value > range[1]) {
        error(`${field.name} = ${value} is not a whole number ${range[0]}..${range[1]}.`, 'ERR_VM_BLK_BAD_SHAPE')
        continue
      }
      if (field.cType === 'uint8_t') view.setUint8(field.offset, value)
      else if (field.cType === 'uint16_t') view.setUint16(field.offset, value, true)
      else if (field.cType === 'uint32_t') view.setUint32(field.offset, value, true)
      else if (field.cType === 'int32_t') view.setInt32(field.offset, value, true)
      else view.setBigUint64(field.offset, BigInt(value), true)
    }
    const rule = TYPE_RULES[type.key]?.((pin) => inputs[pin] != null, (name) => settings.get(name) ?? 0)
    if (rule) error(rule, 'ERR_VM_BLK_BAD_SHAPE')
    if (custom.byteLength > 0xffff) error(`its state takes ${custom.byteLength} bytes, at most 65535.`)

    const head = catalog.wire.addBlock.recordSize
    const pins = [...inputIds, ...objectIds, ...enableIds]
    const record = new Uint8Array(head + pins.length * 2 + custom.byteLength)
    const out = new DataView(record.buffer)
    out.setUint16(0, wireId, true)
    out.setUint16(2, wireId, true) // block_idx: the device names the block by it in errors
    record[4] = type.id
    record[5] = inputIds.length
    record[6] = objectIds.length
    record[7] = enableIds.length
    record[8] = catalog.enModes.get((block.enableMode ?? 'any').toUpperCase()) ?? 0
    record[9] = catalog.onErrors.get((block.onError ?? 'stop').toUpperCase()) ?? 0
    out.setUint16(10, custom.byteLength, true)
    out.setUint16(12, enoId, true)
    pins.forEach((id, index) => out.setUint16(head + index * 2, id, true))
    record.set(custom, head + pins.length * 2)
    records.push(record)
    arenaBytes += align(catalog.arena.blockHead + pins.length * catalog.arena.pointer + custom.byteLength, catalog.arena.alignment)
    placed.push({ wireId, id: block.id, type: type.key, inputs: inputIds, outputs: objectIds, enables: enableIds, ...(eno ? { eno: enoId } : {}), ...(span ? { span } : {}) })
  }

  return { layout: { blocks: placed, wireIdOf: new Map(placed.map((entry) => [entry.id, entry.wireId])) }, diagnostics, records, arenaBytes }
}
