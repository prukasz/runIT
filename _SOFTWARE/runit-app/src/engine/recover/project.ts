import type { Catalog } from '../catalog/catalog'
import { DEVICE_STATE } from '../catalog/layouts'
import { shapeOf } from '../catalog/shape'
import type { BlockType, Field, FieldScalar, PinGroup, Variant } from '../catalog/types'
import type { ExpressionLanguage } from '../expression'
import type { Diagnostic } from '../model/program/diagnostics'
import type { ObjectPath, PathStep } from '../model/paths/paths'
import { concretePins } from '../model/program/pins'
import type { BlockInstance, Project, Source } from '../model/program/program'
import { locate } from '../model/variables/variables'
import type { FolderVariable, ReferenceVariable, ValueVariable, Variable } from '../model/variables/variables'
import { decodeValue } from '../wire/format/codec'
import type { AccessorDef, BlockDef, ObjectDef, ProgramIR } from '../wire/format/program'
import { ID_NONE, OBJ_WIDTH } from '../wire/format/types'

/*
 * A program back into a project. The board keeps the program, not the project: what is lost is what the program never held
 * (descriptions, the names of blocks and of a switch's cases, ids, positions on a canvas); what the program holds is
 * rebuilt:
 *
 *   variables   the named objects. A pointer object is a folder when its children are the objects that follow it, depth first
 *               (how `layoutVariables` numbers them), else a reference to its one child. Objects without a name are the ones
 *               blocks own: they come back as the outputs of their block
 *   paths       an accessor is its root's path in the tree plus its steps (a fixed position is an index)
 *   blocks      one instance per block record, in execution order: the type, the operation of a device block, its pins
 *               (inputs by slot, enables, the variable an output also writes), its fields read out of the state at their
 *               offsets, a formula decompiled to its text
 *
 * Variables and blocks get ids made from their place on the board (`v<object>`, `b<block>`). Compiling the result gives the
 * same program again; `Engine.recover` checks that and says so.
 */

export interface RecoverContext {
  readonly catalog: Catalog
  /** The formula language of a block type, for a block with a formula editor. */
  readonly language: (type: BlockType) => ExpressionLanguage | undefined
}

export interface Recovered {
  readonly project: Project
  /** What could not be read back or was guessed. */
  readonly diagnostics: readonly Diagnostic[]
}

const variableId = (object: number): string => `v${object}`
const blockId = (block: number): string => `b${block}`

const view = (bytes: Uint8Array): DataView => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)

const readRaw = (state: Uint8Array, offset: number, raw: NonNullable<Field['rawType']>): number | undefined => {
  const widths: Record<string, number> = { u8: 1, i8: 1, u16: 2, i16: 2, u32: 4, i32: 4, float: 4 }
  const width = widths[raw]
  if (width === undefined || offset + width > state.length) return undefined
  const v = view(state)
  switch (raw) {
    case 'u8': return v.getUint8(offset)
    case 'i8': return v.getInt8(offset)
    case 'u16': return v.getUint16(offset, true)
    case 'i16': return v.getInt16(offset, true)
    case 'u32': return v.getUint32(offset, true)
    case 'i32': return v.getInt32(offset, true)
    default: return v.getFloat32(offset, true)
  }
}

/** The value the user would have typed for a field whose wire number is `raw`. */
const shownValue = (field: Field, raw: number): FieldScalar => {
  if (field.any !== undefined && raw === field.any) return 'any'
  let shown = field.scale !== undefined && field.scale !== 0 ? raw / field.scale : raw
  if (field.scale !== undefined && field.scale !== 1) shown = Number(shown.toPrecision(9))
  if (field.type === 'enum' && field.options) {
    const option = field.options.find((o) => o.value === shown || String(o.value) === String(shown))
    if (option) return option.value
  }
  if (field.type === 'boolean') return shown !== 0
  return shown
}

export const recoverProject = (ctx: RecoverContext, ir: ProgramIR): Recovered => {
  const diagnostics: Diagnostic[] = []
  const note = (severity: Diagnostic['severity'], code: string, message: string, where: Diagnostic['where']) => diagnostics.push({ severity, code, message, where })
  const objects = new Map(ir.objects.map((o) => [o.id, o]))
  const accessors = new Map(ir.accessors.map((a) => [a.id, a]))

  /* ---- the values of the objects, from the SET_DATA records ---- */
  const bytesOf = new Map<number, Uint8Array>()
  for (const object of ir.objects) {
    const width = OBJ_WIDTH[object.type].wire
    if (ir.data.some((d) => d.id === object.id)) bytesOf.set(object.id, new Uint8Array(object.count * width))
  }
  for (const d of ir.data) {
    const object = objects.get(d.id)
    const target = bytesOf.get(d.id)
    if (!object || !target) {
      note('warning', 'value-without-object', `A value for object ${d.id}, which the program does not define.`, { kind: 'program' })
      continue
    }
    target.set(d.data.subarray(0, Math.max(0, target.length - d.start * OBJ_WIDTH[object.type].wire)), d.start * OBJ_WIDTH[object.type].wire)
  }
  const childrenOf = (object: ObjectDef): number[] => {
    const bytes = bytesOf.get(object.id)
    if (!bytes) return []
    const ids: number[] = []
    for (let at = 0; at + 2 <= bytes.length; at += 2) ids.push(view(bytes).getUint16(at, true))
    return ids.slice(0, object.count)
  }

  /* ---- variables: the named objects ---- */
  const claimed = new Set<number>()
  const subtree = (id: number): { variable: Variable; last: number } => {
    const object = objects.get(id)!
    claimed.add(id)
    const base = { id: variableId(id), name: object.name }
    if (object.type !== 'ptr') {
      const bytes = bytesOf.get(id)
      let initial: ValueVariable['initial']
      if (bytes) {
        if (object.type === 'str') initial = new TextDecoder().decode(bytes).replace(/\0+$/, '')
        else {
          const value = decodeValue(object.type, bytes)
          if (!(value instanceof Uint8Array)) initial = value as ValueVariable['initial']
        }
      }
      const variable: ValueVariable = {
        kind: 'value',
        ...base,
        type: object.type,
        count: object.type === 'str' ? object.count : object.count,
        ...(initial !== undefined ? { initial } : {}),
        ...(object.flags.retentive ? { retained: true } : {}),
        ...(object.flags.usrProtected ? { protected: true } : {}),
        ...(object.flags.mutable === false ? { writable: false } : {}),
      }
      return { variable, last: id }
    }
    const kids = childrenOf(object)
    let next = id + 1
    const built: Variable[] = []
    let isFolder = kids.length > 0
    for (const kid of kids) {
      if (kid !== next || !objects.has(kid) || claimed.has(kid)) {
        isFolder = false
        break
      }
      const sub = subtree(kid)
      built.push(sub.variable)
      next = sub.last + 1
    }
    if (isFolder) return { variable: { kind: 'folder', ...base, children: built } satisfies FolderVariable, last: next - 1 }
    // not a folder: forget the children it claimed on the way, it points at one object elsewhere
    for (const child of built.flatMap((v) => flatten(v))) claimed.delete(Number(child.slice(1)))
    const target = kids[0]
    return { variable: { kind: 'reference', ...base, target: target === undefined ? null : variableId(target) } satisfies ReferenceVariable, last: id }
  }
  const flatten = (v: Variable): string[] => (v.kind === 'folder' ? [v.id, ...v.children.flatMap(flatten)] : [v.id])

  const variables: Variable[] = []
  for (const object of ir.objects) {
    if (object.name === '' || claimed.has(object.id)) continue
    variables.push(subtree(object.id).variable)
  }

  /* ---- who owns the objects without a name: the blocks that write them ---- */
  const blocks = [...ir.blocks].sort((a, b) => a.id - b.id)
  const owner = new Map<number, { block: string; pin: number | 'eno' }>()
  for (const block of blocks) {
    block.outputs.forEach((object, pin) => {
      if (objects.get(object)?.name === '') owner.set(object, { block: blockId(block.id), pin })
    })
    if (block.eno !== ID_NONE) owner.set(block.eno, { block: blockId(block.id), pin: 'eno' })
  }

  /* ---- paths ---- */
  const pathOfObject = (object: number): ObjectPath | undefined => {
    const found = locate(variables, variableId(object))
    if (!found) return undefined
    const chain = [...found.ancestors, found.variable]
    return { root: chain[0]!.id, steps: chain.slice(1).map((v): PathStep => ({ kind: 'name', name: v.name })) }
  }
  const pathOfAccessor = (accessor: AccessorDef, depth = 0): ObjectPath | undefined => {
    const base = pathOfObject(accessor.root)
    if (!base || depth > 8) return undefined
    const steps: PathStep[] = [...base.steps]
    for (const step of accessor.steps) {
      if (step.kind === 'literal') steps.push({ kind: 'index', index: step.value })
      else if (step.kind === 'name') steps.push({ kind: 'name', name: step.name })
      else {
        const inner = accessors.get(step.accessor)
        const path = inner && pathOfAccessor(inner, depth + 1)
        if (!path) return undefined
        steps.push({ kind: 'dynamic', path })
      }
    }
    return { root: base.root, steps }
  }
  const sourceOf = (accessorId: number, where: Diagnostic['where']): Source | null => {
    if (accessorId === ID_NONE) return null
    const accessor = accessors.get(accessorId)
    if (!accessor) {
      note('error', 'accessor-missing', `Accessor ${accessorId} is not in the program.`, where)
      return null
    }
    const own = owner.get(accessor.root)
    if (own && accessor.steps.length === 0) return own.pin === 'eno' ? { kind: 'eno', block: own.block } : { kind: 'output', block: own.block, pin: own.pin }
    const path = pathOfAccessor(accessor)
    if (!path) {
      note('error', 'accessor-unreadable', `Accessor ${accessorId} reads through something that is not a variable.`, where)
      return null
    }
    return { kind: 'variable', path }
  }

  /* ---- blocks ---- */
  const instances: BlockInstance[] = []
  for (const block of blocks) {
    const id = blockId(block.id)
    const where: Diagnostic['where'] = { kind: 'block', id }
    const type = ctx.catalog.byWireId(block.type)
    if (!type) {
      note('error', 'unknown-block', `Block ${block.id} is of type ${block.type}, which the catalog does not have.`, where)
      continue
    }
    const instance = recoverBlock(ctx, type, block, id, where, sourceOf, pathOfObject, objects, note)
    if (instance) instances.push(instance)
  }

  return { project: { variables, blocks: instances }, diagnostics }
}

type Note = (severity: Diagnostic['severity'], code: string, message: string, where: Diagnostic['where']) => void

const recoverBlock = (
  ctx: RecoverContext,
  type: BlockType,
  block: BlockDef,
  id: string,
  where: Diagnostic['where'],
  sourceOf: (accessor: number, where: Diagnostic['where']) => Source | null,
  pathOfObject: (object: number) => ObjectPath | undefined,
  objects: ReadonlyMap<number, ObjectDef>,
  note: Note,
): BlockInstance | undefined => {
  const isDevice = type.integration !== 'core'
  const state = block.state

  let variant: Variant | undefined
  let device: number | null | undefined
  if (isDevice) {
    device = readRaw(state, DEVICE_STATE.deviceId, 'u16')
    const op = readRaw(state, DEVICE_STATE.op, 'u16')
    variant = type.variants.find((v) => v.operation?.value === op)
    if (!variant) {
      note('error', 'unknown-operation', `Block ${block.id} runs operation ${op}, which '${type.alias}' does not have.`, where)
      return undefined
    }
  } else variant = type.variants[0]

  // pin groups: the pins the user added are the slots beyond the fixed ones
  const shape = shapeOf(type, variant?.id)
  const groupOf = (specs: typeof shape.inputs): PinGroup | undefined => specs.find((s): s is PinGroup => s.kind === 'group')
  const fixedCount = (specs: typeof shape.inputs): number => specs.filter((s) => s.kind === 'pin').length
  const inGroup = groupOf(shape.inputs)
  const outGroup = groupOf(shape.outputs)
  const groups = {
    ...(inGroup ? { inputs: Math.max(inGroup.min, Math.min(inGroup.max, block.inputs.length - fixedCount(shape.inputs))) } : {}),
    ...(outGroup ? { outputs: Math.max(outGroup.min, Math.min(outGroup.max, block.outputs.length - fixedCount(shape.outputs))) } : {}),
  }
  const pins = concretePins(type, { variant: variant?.id, groups })

  // inputs: the slot of an input pin is its position, except on a device block, where it is the argument's place
  const slotOf = (index: number): number => {
    if (!isDevice) return index
    const field = pins.inputs[index]?.pin.field
    return variant && field !== undefined ? variant.fields.findIndex((f) => f.id === field) : index
  }
  const wired = new Set<string>()
  const inputs: (Source | null)[] = pins.inputs.map((cp, i) => {
    const source = sourceOf(block.inputs[slotOf(i)] ?? ID_NONE, where)
    if (source && cp.pin.field !== undefined) wired.add(cp.pin.field)
    return source
  })
  while (inputs.length > 0 && inputs[inputs.length - 1] === null) inputs.pop()

  const enables = block.enables.map((a) => sourceOf(a, where)).filter((s): s is Source => s !== null)
  const outputs = block.outputs.map((object) => {
    if (objects.get(object)?.name === '') return null
    const path = pathOfObject(object)
    return path ? { path } : null
  })
  while (outputs.length > 0 && outputs[outputs.length - 1] === null) outputs.pop()

  // fields: read out of the state at their offsets
  const values: Record<string, FieldScalar> = {}
  const readFields = (fields: readonly Field[]) => {
    for (const field of fields) {
      if (field.fixed !== undefined || field.offset === undefined || field.rawType === undefined || field.rawType === 'bytes') continue
      if (wired.has(field.id)) continue
      const raw = readRaw(state, field.offset, field.rawType)
      if (raw === undefined) continue
      if (raw === 0 && !field.required && field.default === undefined) continue
      const shown = shownValue(field, raw)
      if (field.default !== undefined && shown === field.default) continue
      values[field.id] = shown
    }
  }
  readFields(type.fields)
  if (!isDevice) readFields(variant?.fields ?? [])
  else if (variant) {
    variant.fields.forEach((field, slot) => {
      if (field.fixed !== undefined || wired.has(field.id)) return
      const k = readRaw(state, DEVICE_STATE.k + slot * 4, 'i32')
      if (k === undefined || (k === 0 && !field.required && field.default === undefined)) return
      const scaled = field.scale !== undefined && field.scale !== 0 && field.scale !== 1
      const raw = scaled ? view(state).getFloat32(DEVICE_STATE.k + slot * 4, true) : k
      const shown = shownValue({ ...field, scale: undefined, rawType: undefined }, raw)
      if (field.default !== undefined && shown === field.default) return
      values[field.id] = field.type === 'boolean' ? raw !== 0 : shown
    })
  }

  // a formula
  let editor: unknown
  const language = type.stateVariable ? ctx.language(type) : undefined
  if (type.stateVariable) {
    const text = language?.decompile(state)
    if (language && text?.ok) editor = { text: text.text }
    else note('warning', 'editor-unreadable', `The state of block ${block.id} could not be read back as a formula${text && !text.ok ? `: ${text.reason}` : ''}.`, where)
  }

  return {
    id,
    type: type.id,
    ...(variant && type.variants.length > 0 ? { variant: variant.id } : {}),
    ...(isDevice ? { device: device ?? null } : {}),
    ...(Object.keys(groups).length > 0 ? { groups } : {}),
    inputs,
    outputs,
    enables,
    ...(block.enableMode !== 'any' ? { enableMode: block.enableMode } : {}),
    ...(block.onError !== 'continue' ? { onError: block.onError } : {}),
    ...(Object.keys(values).length > 0 ? { values } : {}),
    ...(editor !== undefined ? { editor } : {}),
  }
}
