import type { VmCatalog } from '../descriptors'
import type { ObjectNode, ObjectPath } from '../project'
import type { Diagnostic, ObjectLayout, PlacedObject } from './objects'

/*
 * Object paths (block pins, enable sources) → VM accessors: wire IDs, 0x44
 * records and arena bytes, with what the device would refuse or fail on
 * checked first.
 *
 * - Nested: a step that lands on a folder's child goes into the child. Where
 *   that child is a fixed program object, the path is re-rooted on it, so
 *   `motor[0][2]` becomes `speed[2]`: shorter, and a root with at most one
 *   literal step is one the device caches (VM.MD "Accessors"). A path that
 *   ends on a child keeps the folder step: it names the slot, not the child.
 * - Dynamic: the position is another path, compiled to its own accessor and
 *   read on every use (VM_IDX_REF). It gets the lower wire ID, so it exists
 *   when the device reads the record that refers to it.
 * - Name steps into a known folder become literal positions. They stay names
 *   only where the shape is unknown at compile time: under a live cell or a
 *   dynamic child.
 * - Live cells: folder slots a block re-links at run time (CLONE's target).
 *   Nothing below them is re-rooted or checked.
 * - Identical paths share one accessor (dedup is the compiler's job).
 */

export interface AccessorRequest {
  /** Who uses the path, e.g. `block:<id>:in0`; returned in the layout and on diagnostics. */
  readonly key: string
  readonly path: ObjectPath
}

export type WireStep =
  | { readonly kind: 'literal'; readonly value: number }
  | { readonly kind: 'ref'; readonly accessor: number }
  | { readonly kind: 'name'; readonly name: string }

export interface PlacedAccessor {
  readonly wireId: number
  /** Root object wire ID. */
  readonly root: number
  readonly steps: readonly WireStep[]
  /** The device pre-resolves it at load: the whole object or one literal step. */
  readonly cached: boolean
  /** Request keys that use it, directly (not as the index of another path). */
  readonly keys: readonly string[]
}

export interface AccessorLayout {
  /** Index = wire ID. */
  readonly accessors: readonly PlacedAccessor[]
  /** Request key → accessor wire ID. */
  readonly wireIdOf: ReadonlyMap<string, number>
}

export interface CompiledAccessors {
  readonly layout: AccessorLayout
  readonly diagnostics: readonly Diagnostic[]
  /** 0x44 records in wire ID order: u16 acc_id, u16 root_obj_id, u8 idx_count, u8 idx_len, idx_data. */
  readonly records: readonly Uint8Array[]
  /** Arena bytes of the accessors, their names and their registry. */
  readonly arenaBytes: number
}

export interface AccessorOptions {
  /** Project IDs of folder entries (the child or reference in the folder) whose slot a block re-links at run time. */
  readonly liveCells?: ReadonlySet<string>
}

/** What a compiled path reaches, as far as the compiler knows. */
interface Reach {
  readonly wireId: number
  /** Nesting of dynamic steps: 1 = no dynamic step. */
  readonly depth: number
  /** The object indexed last (undefined past a live cell or a dynamic child). */
  readonly object?: PlacedObject
  /** A step picked an element of `object` (else the path names the whole object). */
  readonly element: boolean
}

const ascii = new TextEncoder()
const PRINTABLE = /^[\x20-\x7e]+$/
const U8_MAX = 0xff
const U32_MAX = 0xffffffff

const align = (size: number, to: number): number => Math.ceil(size / to) * to

const describe = (object: PlacedObject): string => `'${object.node.name || `object ${object.wireId}`}'`

export const compileAccessors = (requests: readonly AccessorRequest[], objects: ObjectLayout, catalog: VmCatalog, options: AccessorOptions = {}): CompiledAccessors => {
  const liveCells = options.liveCells ?? new Set<string>()
  const diagnostics: Diagnostic[] = []
  const placed: { root: number; steps: WireStep[]; keys: string[] }[] = []
  const byShape = new Map<string, number>()
  const wireIdOf = new Map<string, number>()

  /** The folder's uploaded entries, aligned with its children's wire IDs. */
  const entriesOf = (folder: PlacedObject): readonly ObjectNode[] =>
    folder.node.kind === 'folder' ? folder.node.children.filter((child) => objects.wireIdOf.has(child.id)) : []

  const { literal, ref, name } = catalog.indexKinds
  const stepBytes = (step: WireStep): number => step.kind === 'literal' ? literal.size : step.kind === 'ref' ? ref.size : name.size + step.name.length
  const indexBytes = (steps: readonly WireStep[]): number => steps.reduce((sum, step) => sum + stepBytes(step), 0)

  const intern = (root: number, steps: WireStep[]): number => {
    const shape = `${root}|${steps.map((step) => step.kind === 'literal' ? `L${step.value}` : step.kind === 'ref' ? `R${step.accessor}` : `N${step.name}`).join(',')}`
    const known = byShape.get(shape)
    if (known !== undefined) return known
    byShape.set(shape, placed.length)
    placed.push({ root, steps, keys: [] })
    return placed.length - 1
  }

  const compilePath = (path: ObjectPath, key: string, where: string): Reach | undefined => {
    const fail = (message: string, firmwareError?: string): undefined => {
      diagnostics.push({ severity: 'error', pathKey: key, message: `${where}: ${message}`, ...(firmwareError ? { firmwareError } : {}) })
      return undefined
    }
    const rootId = objects.wireIdOf.get(path.root)
    if (rootId === undefined) return fail(`'${path.root}' is not an uploaded object.`)

    let root = rootId
    let steps: WireStep[] = []
    let object: PlacedObject | undefined = objects.objects[rootId]
    let element = false
    /** The fixed child an element step landed on, when the path may be re-rooted on it. */
    let child: PlacedObject | undefined
    let depth = 1

    for (const [index, step] of (path.steps ?? []).entries()) {
      const at = `step ${index + 1}`
      if (element) {
        // The previous step picked an element: this one goes into it, which only a folder's child allows.
        if (object && object.type.key !== 'PTR') return fail(`${at}: an element of ${describe(object)} (${object.type.alias}) has nothing below it.`, 'ERR_VM_ACCESSOR_TYPE_MISMATCH')
        if (child) {
          root = child.wireId
          steps = []
        }
        object = child
        element = false
      }
      child = undefined
      const folder = object?.type.key === 'PTR' ? object : undefined
      const liveEntry = (position: number): boolean => !!folder && liveCells.has(entriesOf(folder)[position]?.id ?? '')
      const fixedChild = (position: number): PlacedObject | undefined =>
        folder && !liveEntry(position) ? objects.objects[folder.children?.[position] ?? -1] : undefined

      switch (step.kind) {
        case 'index': {
          if (!Number.isInteger(step.index) || step.index < 0 || step.index > U32_MAX) return fail(`${at}: position ${step.index} is not a whole number 0..${U32_MAX}.`)
          if (object && step.index >= object.elements) return fail(`${at}: ${describe(object)} has ${object.elements} ${folder ? 'entries' : 'elements'}, no position ${step.index}.`, 'ERR_VM_ACCESSOR_OOB')
          steps.push({ kind: 'literal', value: step.index })
          child = fixedChild(step.index)
          break
        }
        case 'name': {
          if (!step.name || !PRINTABLE.test(step.name) || step.name.length > catalog.nameMax) return fail(`${at}: '${step.name}' is not a name (1..${catalog.nameMax} plain ASCII characters).`)
          if (object && !folder) return fail(`${at}: ${describe(object)} is a ${object.type.alias}, only a folder has named children.`, 'ERR_VM_ACCESSOR_TYPE_MISMATCH')
          if (!folder) {
            steps.push({ kind: 'name', name: step.name })
            break
          }
          const position = (folder.children ?? []).findIndex((id) => objects.objects[id]?.node.name === step.name)
          if (position < 0) return fail(`${at}: ${describe(folder)} has no child '${step.name}'.`, 'ERR_VM_ACCESSOR_NAME_NOT_FOUND')
          // A re-linked slot may hold differently ordered data: keep looking it up by name there.
          steps.push(liveEntry(position) ? { kind: 'name', name: step.name } : { kind: 'literal', value: position })
          child = fixedChild(position)
          break
        }
        case 'dynamic': {
          const source = compilePath(step.index, key, `${where}, ${at} (position)`)
          if (!source) return undefined
          const read = source.object
          if (read?.type.key === 'PTR') return fail(`${at}: the position is read from ${source.element ? `an entry of ${describe(read)}` : `folder ${describe(read)}`}; it must be a number.`, 'ERR_VM_ACCESSOR_TYPE_MISMATCH')
          if (read && !source.element && read.elements > 1) diagnostics.push({ severity: 'warning', pathKey: key, message: `${where}, ${at}: the position is read from ${describe(read)}, which has ${read.elements} elements; only the first is used.` })
          steps.push({ kind: 'ref', accessor: source.wireId })
          depth = Math.max(depth, source.depth + 1)
          break
        }
      }
      element = true
    }

    if (depth > catalog.accessorMaxDepth) return fail(`dynamic positions nest ${depth} deep, the device reads at most ${catalog.accessorMaxDepth}.`, 'ERR_VM_ACCESSOR_DEPTH_EXCEEDED')
    if (steps.length > U8_MAX) return fail(`${steps.length} steps, at most ${U8_MAX}.`)
    if (indexBytes(steps) > U8_MAX) return fail(`its steps take ${indexBytes(steps)} bytes, at most ${U8_MAX}.`)
    return { wireId: intern(root, steps), depth, object, element }
  }

  for (const request of requests) {
    const reach = compilePath(request.path, request.key, request.key)
    if (!reach) continue
    wireIdOf.set(request.key, reach.wireId)
    placed[reach.wireId]!.keys.push(request.key)
  }

  const records: Uint8Array[] = []
  let arenaBytes = placed.length ? align(placed.length * catalog.arena.pointer, catalog.arena.alignment) : 0
  for (const [wireId, entry] of placed.entries()) {
    const head = catalog.wire.addAccessors.recordSize
    const size = indexBytes(entry.steps)
    const record = new Uint8Array(head + size)
    const view = new DataView(record.buffer)
    view.setUint16(0, wireId, true)
    view.setUint16(2, entry.root, true)
    record[4] = entry.steps.length
    record[5] = size
    let offset = head
    for (const step of entry.steps) {
      if (step.kind === 'literal') {
        record[offset] = literal.value
        view.setUint32(offset + 1, step.value, true)
        offset += literal.size
      } else if (step.kind === 'ref') {
        record[offset] = ref.value
        view.setUint16(offset + 1, step.accessor, true)
        offset += ref.size
      } else {
        const bytes = ascii.encode(step.name)
        record[offset] = name.value
        record[offset + 1] = bytes.byteLength
        record.set(bytes, offset + name.size)
        offset += name.size + bytes.byteLength
        arenaBytes += align(bytes.byteLength + 1, catalog.arena.alignment)
      }
    }
    records.push(record)
    arenaBytes += align(catalog.arena.accessorHead + entry.steps.length * catalog.arena.indexStep, catalog.arena.alignment)
  }

  const accessors = placed.map((entry, wireId): PlacedAccessor => ({
    wireId,
    root: entry.root,
    steps: entry.steps,
    cached: entry.steps.length === 0 || (entry.steps.length === 1 && entry.steps[0]!.kind === 'literal'),
    keys: entry.keys,
  }))
  return { layout: { accessors, wireIdOf }, diagnostics, records, arenaBytes }
}
