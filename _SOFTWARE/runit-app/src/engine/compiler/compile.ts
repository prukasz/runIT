import type { Catalog } from '../catalog/catalog'
import { DEVICE_STATE } from '../catalog/layouts'
import type { BlockType, Pin, ValueKind, Variant } from '../catalog/types'
import { arrange } from '../graph/arrange'
import type { Diagnostic } from '../model/program/diagnostics'
import { hasErrors } from '../model/program/diagnostics'
import { layoutVariables } from '../model/variables/layout'
import { flattenVariables } from '../model/variables/variables'
import type { ObjectPath, Target } from '../model/paths/paths'
import { resolvePath } from '../model/paths/paths'
import { concretePins } from '../model/program/pins'
import type { ConcretePins } from '../model/program/pins'
import type { BlockInstance, Project, Source } from '../model/program/program'
import { checkVariables } from '../model/variables/variables'
import { arenaUsage } from '../wire/load/arena'
import type { ArenaUsage } from '../wire/load/arena'
import { checkProgram } from '../wire/load/check'
import type { WireDefinitions } from '../wire/format/definitions'
import { loadFrames } from '../wire/load/frames'
import type { LoadFrame } from '../wire/load/frames'
import { DEFAULT_LIMITS, frameCapacity } from '../wire/load/limits'
import type { VmLimits } from '../wire/load/limits'
import type { BlockDef, DataDef, ObjectDef, ProgramIR } from '../wire/format/program'
import { ID_NONE } from '../wire/format/types'
import type { ObjType } from '../wire/format/types'
import { AccessorTable, cellContainer, compilePath } from './accessors'
import type { PathContext } from './accessors'
import type { CodecRegistry } from './block/codecs'
import { encodeState } from './block/state'

/*
 * Project -> program. The order of work:
 *
 *   1. the variables are checked and laid out as objects (ids 0..n-1);
 *   2. the blocks are put in execution order (graph/arrange.ts), loops with their spans;
 *   3. every block gets the objects it writes: the user variable an output was given, else an object of its own, and one
 *      for its enable-out when something reads it;
 *   4. every pin that reads becomes an accessor (compiler/accessors.ts), each block's state is encoded, and the program is
 *      held against what the board checks at load (wire/check.ts).
 *
 * Nothing throws for a mistake in the project: the mistakes are the returned diagnostics, and a program with any error has no
 * frames. The block's position in the order is its id on the board.
 */

export interface CompileContext {
  readonly catalog: Catalog
  readonly wire: WireDefinitions
  readonly codecs: CodecRegistry
  readonly limits?: VmLimits
}

export interface CompileOptions {
  /** Arena bytes to ask the board for; default is what the program needs. */
  readonly arenaBytes?: number
  /** Give every block an enable-out object, not only those something reads (what a debug build wants to watch). */
  readonly allEnableOuts?: boolean
}

export interface CompileResult {
  /** No errors: the program can be uploaded. */
  readonly ok: boolean
  readonly diagnostics: readonly Diagnostic[]
  readonly ir: ProgramIR | undefined
  readonly frames: readonly LoadFrame[]
  readonly arena: ArenaUsage | undefined
  readonly ids: {
    /** Variable id -> object id. */
    readonly variables: ReadonlyMap<string, number>
    /** Block instance id -> block id on the board (its place in the execution order). */
    readonly blocks: ReadonlyMap<string, number>
    /** `<block>:out<n>` / `<block>:eno` -> the object the block writes there (a user variable's, or one of the block's own). */
    readonly outputs: ReadonlyMap<string, number>
  }
  /** Block instance ids in execution order. */
  readonly order: readonly string[]
}

const ownedType = (value: ValueKind): ObjType => (value === 'float' || value === 'any' ? 'f32' : value === 'int' ? 'i32' : 'bool')

export const compile = (ctx: CompileContext, project: Project, options: CompileOptions = {}): CompileResult => {
  const limits = ctx.limits ?? DEFAULT_LIMITS
  const diagnostics: Diagnostic[] = []
  const report = (severity: Diagnostic['severity'], code: string, message: string, where: Diagnostic['where']) => diagnostics.push({ severity, code, message, where })
  const onBlock = (id: string) => (severity: Diagnostic['severity'], code: string, message: string, extra: Partial<Diagnostic['where']> = {}) =>
    report(severity, code, message, { kind: 'block', id, ...extra })

  /* 1. variables */
  for (const p of checkVariables(project.variables)) report('error', p.code, p.message, { kind: 'variable', id: p.id })
  const layout = layoutVariables(project.variables)
  for (const s of layout.skipped) report('warning', 'not-in-program', `Left out of the program: ${s.reason}.`, { kind: 'variable', id: s.id })

  /* 2. blocks in execution order */
  const arrangement = arrange(ctx.catalog, project, limits)
  diagnostics.push(...arrangement.diagnostics)
  const byId = new Map(project.blocks.map((b) => [b.id, b]))
  const position = new Map(arrangement.order.map((id, i) => [id, i]))

  interface Resolved {
    readonly instance: BlockInstance
    readonly type: BlockType
    readonly variant: Variant | undefined
    readonly pins: ConcretePins
  }
  const resolved = new Map<string, Resolved>()
  for (const instance of project.blocks) {
    const here = onBlock(instance.id)
    const type = ctx.catalog.get(instance.type)
    if (!type) {
      here('error', 'unknown-block', `There is no block type '${instance.type}'.`)
      continue
    }
    let variant: Variant | undefined
    if (type.variants.length > 0) {
      variant = type.variants.find((v) => v.id === instance.variant)
      if (!variant) {
        if (instance.variant !== undefined) here('error', 'unknown-variant', `'${type.alias}' has no '${instance.variant}'.`)
        variant = type.variants[0]
      }
    }
    const pins = concretePins(type, { variant: variant?.id, groups: instance.groups })
    resolved.set(instance.id, { instance, type, variant, pins })
  }

  /* what is read: which outputs and enable-outs other blocks use */
  const usedOutputs = new Map<string, Set<number>>()
  const usedEno = new Set<string>()
  const noteUse = (source: Source) => {
    if (source.kind === 'output') {
      const set = usedOutputs.get(source.block)
      if (set) set.add(source.pin)
      else usedOutputs.set(source.block, new Set([source.pin]))
    } else if (source.kind === 'eno') usedEno.add(source.block)
  }
  for (const instance of project.blocks) {
    for (const s of instance.inputs) if (s) noteUse(s)
    for (const s of instance.enables) noteUse(s)
  }

  /* 3. the objects blocks write */
  const objects: ObjectDef[] = [...layout.objects]
  const data: DataDef[] = [...layout.data]
  const outputs = new Map<string, number>()
  const outputCount = new Map<string, number>()
  const tree = project.variables

  /** A whole-variable target: the object an output pin writes. */
  const targetObject = (blockId: string, pin: number, path: ObjectPath): number | undefined => {
    const here = onBlock(blockId)
    const found = resolvePath(tree, path)
    if (!found.ok) {
      here('error', found.code, found.message, { pin: { side: 'out', index: pin } })
      return undefined
    }
    const t = found.target
    if (t.element !== null || t.dynamic || t.variable.kind !== 'value') {
      here('error', 'output-not-a-variable', 'An output writes a whole variable (a number or a list), not an element, a folder or a run-time position.', { pin: { side: 'out', index: pin } })
      return undefined
    }
    const id = layout.ids.get(t.variable.id)
    if (id === undefined) here('error', 'not-in-program', `'${t.variable.name}' is not part of the program.`, { pin: { side: 'out', index: pin } })
    return id
  }

  const newObject = (type: ObjType): number => {
    const id = objects.length
    objects.push({ id, type, count: 1, flags: { mutable: true, updResetable: true, usrProtected: true }, name: '' })
    return id
  }

  for (const id of arrangement.order) {
    const r = resolved.get(id)
    if (!r) continue
    const { instance, pins } = r
    const used = usedOutputs.get(id)
    let lastUsed = -1
    for (let i = 0; i < pins.outputs.length; i++) if (instance.outputs[i] || used?.has(i)) lastUsed = i
    const minQ = r.pins.shape.minQ
    const count = Math.min(pins.outputs.length, Math.max(minQ, lastUsed + 1))
    outputCount.set(id, count)
    for (let i = 0; i < count; i++) {
      const target = instance.outputs[i]
      const object = target ? targetObject(id, i, target.path) : newObject(ownedType(pins.outputs[i]!.pin.value))
      if (object !== undefined) outputs.set(`${id}:out${i}`, object)
    }
    if (usedEno.has(id) || options.allEnableOuts) outputs.set(`${id}:eno`, newObject('bool'))
  }

  /* cells a block may re-link, and what the user marks as known only at run time: no path is folded through them */
  const live = new Set<string>(flattenVariables(tree).filter((v) => (v.kind === 'folder' || v.kind === 'reference') && v.dynamic).map((v) => v.id))
  for (const r of resolved.values()) {
    r.instance.inputs.forEach((source, i) => {
      if (source?.kind !== 'variable' || r.pins.inputs[i]?.pin.value !== 'cell') return
      const folder = cellContainer(tree, source.path)
      if (folder) live.add(folder)
    })
  }

  /* 4. accessors, states, blocks */
  const table = new AccessorTable()
  const pathCtx: PathContext = { tree, layout, live, table }

  /** The accessor a source reads through, or undefined (the reason reported). */
  const accessorFor = (blockId: string, side: 'in' | 'en', index: number, pin: Pin | undefined, source: Source): number | undefined => {
    const here = onBlock(blockId)
    const at = { pin: { side, index } }
    if (source.kind === 'variable') {
      const result = compilePath(pathCtx, source.path, pin?.value === 'cell')
      if (!result.ok) {
        here('error', result.code, result.message, at)
        return undefined
      }
      const t: Target = result.target
      if (pin && (pin.value === 'bool' || pin.value === 'int' || pin.value === 'float' || pin.value === 'any') && t.type === 'ptr') {
        here('error', 'ERR_VM_ACCESSOR_TYPE_MISMATCH', `'${t.variable.name}' is a folder: this pin reads a number.`, at)
        return undefined
      }
      if (side === 'en' && t.type === 'ptr') {
        here('error', 'ERR_VM_ACCESSOR_TYPE_MISMATCH', `'${t.variable.name}' is a folder: an enable reads a true / false value.`, at)
        return undefined
      }
      if (pin?.value === 'cell' && cellContainer(tree, source.path) === undefined) {
        here('error', 'cell-needs-folder-entry', 'This pin needs an entry of a folder: the place the block puts what it builds.', at)
        return undefined
      }
      if (pin?.writes) {
        const v = t.variable
        const writable = v.kind === 'value' ? v.writable !== false : true
        if (!writable) {
          here('error', 'ERR_VM_OBJ_NOT_MUTABLE', `'${v.name}' is a constant: the block cannot write it.`, at)
          return undefined
        }
      }
      return result.accessor
    }

    const producer = byId.get(source.block)
    const producerResolved = resolved.get(source.block)
    if (!producer || !producerResolved) {
      here('error', 'unknown-block', `This pin reads block '${source.block}', which does not exist.`, at)
      return undefined
    }
    if (source.kind === 'eno') {
      const object = outputs.get(`${source.block}:eno`)
      if (object === undefined) return undefined
      return table.whole(object)
    }
    const object = outputs.get(`${source.block}:out${source.pin}`)
    if (object === undefined) {
      here('error', 'pin-missing', `Block '${producer.name ?? producerResolved.type.alias}' has no output ${source.pin}.`, at)
      return undefined
    }
    return table.whole(object)
  }

  const blocks: BlockDef[] = []
  for (const id of arrangement.order) {
    const r = resolved.get(id)
    if (!r) continue
    const { instance, type, variant, pins } = r
    const here = onBlock(id)
    const place = position.get(id)!

    if (instance.inputs.length > pins.inputs.length) here('error', 'pin-missing', `${instance.inputs.length} inputs wired; the block has ${pins.inputs.length}.`)
    if (instance.groups?.inputs !== undefined && pins.inputs.length === 0) here('warning', 'no-group', 'This block has no inputs that can be added.')

    // inputs: the slot of an input pin on the board is its position, except on a device block, where it is the argument's place
    const isDevice = type.integration !== 'core'
    const slotOf = (pinIndex: number): number => {
      if (!isDevice) return pinIndex
      const field = pins.inputs[pinIndex]?.pin.field
      return variant && field !== undefined ? variant.fields.findIndex((f) => f.id === field) : pinIndex
    }
    const slots: number[] = []
    const wiredFields = new Set<string>()
    pins.inputs.forEach((cp, i) => {
      const source = instance.inputs[i] ?? null
      if (!source) {
        if (cp.pin.required) here('error', 'pin-unwired', `'${cp.pin.alias}' must be wired.`, { pin: { side: 'in', index: i } })
        return
      }
      const accessor = accessorFor(id, 'in', i, cp.pin, source)
      if (accessor === undefined) return
      const slot = slotOf(i)
      slots[slot] = accessor
      if (cp.pin.field !== undefined) wiredFields.add(cp.pin.field)
    })
    const inCount = slots.length
    const inputIds: number[] = Array.from({ length: inCount }, (_, i) => slots[i] ?? ID_NONE)
    if (isDevice && inCount > DEVICE_STATE.fields) here('error', 'too-many-arguments', `A device block carries ${DEVICE_STATE.fields} arguments.`)

    // enables
    const enableIds: number[] = []
    instance.enables.forEach((source, i) => {
      const accessor = accessorFor(id, 'en', i, undefined, source)
      if (accessor !== undefined) enableIds.push(accessor)
    })
    if (pins.shape.minEn > instance.enables.length) {
      here('error', 'enable-required', `'${type.enable?.alias ?? 'Enable'}' must be wired: the block does nothing without it.`, { pin: { side: 'en', index: 0 } })
    }

    // outputs (the objects were made above)
    const count = outputCount.get(id) ?? 0
    const outputIds: number[] = []
    for (let i = 0; i < count; i++) {
      const object = outputs.get(`${id}:out${i}`)
      if (object !== undefined) outputIds.push(object)
    }

    const state = encodeState(
      { type, instance, pins, variant, span: arrangement.spans.get(id), codecs: ctx.codecs, report: onBlock(id) },
      wiredFields,
    )
    if (!state) continue

    blocks.push({
      id: place,
      index: place,
      type: type.wireId,
      inputs: inputIds,
      outputs: outputIds,
      enables: enableIds,
      enableMode: instance.enableMode ?? 'any',
      onError: instance.onError ?? 'continue',
      state,
      eno: outputs.get(`${id}:eno`) ?? ID_NONE,
    })
  }

  /* the program, held against what the board checks */
  const ir: ProgramIR = { objects, data, accessors: table.defs, blocks }
  if (!hasErrors(diagnostics) && blocks.length === project.blocks.length) {
    for (const p of checkProgram(ir, limits)) {
      report('error', p.code, `${p.message} (${p.where.kind}${p.where.id !== undefined ? ` ${p.where.id}` : ''})`, { kind: 'program' })
    }
  }

  let frames = !hasErrors(diagnostics) && blocks.length === project.blocks.length ? loadFrames(ctx.wire, ir, limits, options.arenaBytes) : []
  // a frame is packed to fit, but one record cannot be cut: a block whose pins and state outgrow a frame cannot be loaded at all
  frames.forEach((frame, n) => {
    if (frame.payload.length > frameCapacity(limits)) {
      report('error', 'ERR_ROUTER_FRAME_LEN', `A load frame (${n + 1}) needs ${frame.payload.length} bytes; the board's frame holds ${frameCapacity(limits)}.`, { kind: 'program' })
    }
  })
  const ok = !hasErrors(diagnostics) && blocks.length === project.blocks.length
  if (!ok) frames = []
  return {
    ok,
    diagnostics,
    ir: ok ? ir : undefined,
    frames,
    arena: ok ? arenaUsage(ir) : undefined,
    ids: {
      variables: layout.ids,
      blocks: new Map(arrangement.order.map((id, i) => [id, i])),
      outputs,
    },
    order: arrangement.order,
  }
}
