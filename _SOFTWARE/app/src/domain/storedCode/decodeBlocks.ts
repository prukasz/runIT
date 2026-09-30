import { enumMemberLabels } from '../descriptors'
import type { VmBlockField, VmBlockType, VmCatalog } from '../descriptors'
import type { ObjectPath, PathStep, ProgramBlock } from '../project'
import { pinMaskText } from '../project/blockPins'
import type { UploadDiagnostic } from '../upload'

/*
 * Stored 0x44 (accessors) and 0x45 (blocks) frames → program blocks, the
 * reverse of compiler/accessors.ts and compiler/blocks.ts. What the frames
 * carry comes back: block types, pins as paths, enables, settings (enum
 * members by name), formulas, loop spans. What they don't (block names,
 * positions) is generated: IDs are `<type><n>` in execution order; the
 * canvas layout is domain/canvas/layout.ts.
 */

export type RawStep =
  | { readonly kind: 'literal'; readonly value: number }
  | { readonly kind: 'ref'; readonly accessor: number }
  | { readonly kind: 'name'; readonly name: string }

export interface RawAccessor {
  readonly root: number
  readonly steps: readonly RawStep[]
}

export interface RawBlock {
  readonly wireId: number
  readonly typeId: number
  readonly inCount: number
  readonly outCount: number
  readonly enCount: number
  readonly enMode: number
  readonly onError: number
  /** Object wire ID of the ENO object, or the catalog's "none". */
  readonly eno: number
  /** Input accessor IDs, output object IDs, enable accessor IDs. */
  readonly pins: readonly number[]
  readonly custom: Uint8Array
}

export interface BlockRecovery {
  /** Object IDs of the objects the recovery treats as the blocks' own (outputs, ENO). */
  readonly ownedPins: ReadonlyMap<number, string>
  readonly blocks: readonly ProgramBlock[]
}

interface Source {
  readonly vm: VmCatalog
  readonly blocks: readonly RawBlock[]
  readonly accessors: ReadonlyMap<number, RawAccessor>
  /** Project ID of an object of the user's tree. */
  readonly objectId: (wireId: number) => string
  /** A block's own output or ENO object (not a user object it drives). */
  readonly isOwned: (wireId: number) => boolean
  /** Object type key an accessor's path ends on, when known. */
  readonly typeOfAccessor: (accessorId: number) => string | undefined
  readonly diagnostics: UploadDiagnostic[]
}

const BLOCK_PIN = /^(.+):(q\d+|eno|body)$/
/** Every root a path reads: the path itself and the dynamic indices inside it. */
const roots = (path: ObjectPath): string[] => [path.root, ...(path.steps ?? []).flatMap((step) => (step.kind === 'dynamic' ? roots(step.index) : []))]

const EXPRESSION_COUNTS = new Set(['const_cnt', 'code_len'])

const unsigned = (view: DataView, field: VmBlockField): number | bigint | undefined => {
  switch (field.cType) {
    case 'uint8_t': return view.getUint8(field.offset)
    case 'uint16_t': return view.getUint16(field.offset, true)
    case 'uint32_t': return view.getUint32(field.offset, true)
    case 'int32_t': return view.getInt32(field.offset, true)
    case 'uint64_t': return view.getBigUint64(field.offset, true)
    case 'float': return view.getFloat32(field.offset, true)
    default: return undefined
  }
}

export const recoverBlocks = (source: Source): BlockRecovery => {
  const { vm, diagnostics } = source
  const counters = new Map<string, number>()
  const known = source.blocks.flatMap((raw) => {
    const type = vm.blocks.find((entry) => entry.id === raw.typeId)
    if (!type) {
      diagnostics.push({ severity: 'error', message: `Block ${raw.wireId} has type ${raw.typeId}, which the descriptors don't know; it isn't kept.` })
      return []
    }
    const n = (counters.get(type.key) ?? 0) + 1
    counters.set(type.key, n)
    return [{ raw, type, id: `${type.key.toLowerCase()}${n}` }]
  })

  const ownedPins = new Map<number, string>()
  for (const { raw, id } of known) {
    raw.pins.slice(raw.inCount, raw.inCount + raw.outCount).forEach((object, pin) => {
      if (source.isOwned(object)) ownedPins.set(object, `${id}:q${pin}`)
    })
    if (raw.eno !== vm.blockNoId) ownedPins.set(raw.eno, `${id}:eno`)
  }

  const pathOf = (accessorId: number, depth = 0): ObjectPath | null => {
    const accessor = source.accessors.get(accessorId)
    if (!accessor || depth > vm.accessorMaxDepth + 2) {
      diagnostics.push({ severity: 'error', message: `Accessor ${accessorId} is missing or nests too deep; the pin stays unwired.` })
      return null
    }
    const steps: PathStep[] = []
    for (const step of accessor.steps) {
      if (step.kind === 'literal') steps.push({ kind: 'index', index: step.value })
      else if (step.kind === 'name') steps.push({ kind: 'name', name: step.name })
      else {
        const index = pathOf(step.accessor, depth + 1)
        if (!index) return null
        steps.push({ kind: 'dynamic', index })
      }
    }
    return { root: ownedPins.get(accessor.root) ?? source.objectId(accessor.root), ...(steps.length ? { steps } : {}) }
  }

  const blocks: ProgramBlock[] = []
  const spans: { start: number; end: number; id: string }[] = []
  for (const { raw, type, id } of known) {
    const inputPins = raw.pins.slice(0, raw.inCount)
    const outputPins = raw.pins.slice(raw.inCount, raw.inCount + raw.outCount)
    const enablePins = raw.pins.slice(raw.inCount + raw.outCount)
    const inputs = inputPins.map((accessor) => (accessor === vm.blockNoId ? null : pathOf(accessor)))
    const outputs = outputPins.map((object) => (source.isOwned(object) ? null : source.objectId(object)))
    const enables = enablePins.flatMap((accessor) => {
      const path = pathOf(accessor)
      return path ? [path] : []
    })

    const custom = raw.custom
    const view = new DataView(custom.buffer, custom.byteOffset, custom.byteLength)
    const settings: Record<string, number | string> = {}
    const dynamicInputs: number[] = []
    let expression: ProgramBlock['expression']
    let body: number | undefined

    const counts = new Map<string, number>()
    for (const field of type.fields) {
      if (field.flexible || field.offset + field.size > custom.byteLength) continue
      if (field.source === 'derived') {
        if (field.cType === 'vm_span_t') {
          const start = view.getUint16(field.offset, true)
          const end = view.getUint16(field.offset + 2, true)
          body = Math.max(0, end - start)
          spans.push({ start, end, id })
        }
        continue
      }
      if (field.source !== 'user') continue
      if (type.encoding && EXPRESSION_COUNTS.has(field.name)) {
        counts.set(field.name, Number(unsigned(view, field) ?? 0))
        continue
      }
      if (field.letUserSelectAvailable) {
        const mask = view.getBigUint64(field.offset, true)
        const pin = Number(settings[field.letUserSelectAvailable] ?? unsignedByName(type, view, field.letUserSelectAvailable))
        const dynamicIndex = field.dynamicInput
        const wired = dynamicIndex !== undefined && inputs[dynamicIndex] != null
        if (wired || mask !== 1n << BigInt(pin)) {
          settings[field.name] = pinMaskText(mask)
          if (dynamicIndex !== undefined && !wired) dynamicInputs.push(dynamicIndex)
        }
        continue
      }
      let raw: number | bigint | undefined
      if (field.cType.endsWith('_u') && field.size === 4) {
        const format = source.typeOfAccessor(inputPins[0] ?? vm.blockNoId)
        raw = format === 'F' ? view.getFloat32(field.offset, true) : format === 'I32' ? view.getInt32(field.offset, true) : view.getUint32(field.offset, true)
      } else raw = unsigned(view, field)
      if (raw === undefined) continue
      const value: number | string = typeof raw === 'bigint' ? (raw <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(raw) : pinMaskText(raw)) : raw
      // Plain zeros are the default; an enum keeps its member, so the editor shows the mode.
      if (value === 0 && !field.enumRef) continue
      const member = field.enumRef && typeof value === 'number' ? enumMemberLabels(type.enums.get(field.enumRef) ?? []).find((entry) => entry.value === value) : undefined
      settings[field.name] = member ? member.label : value
    }

    if (type.encoding) {
      const constCount = counts.get('const_cnt') ?? 0
      const codeLength = counts.get('code_len') ?? 0
      const start = Math.max(type.stateSize, type.minCustomLen)
      const constants: number[] = []
      for (let index = 0; index < constCount && start + index * 4 + 4 <= custom.byteLength; index++) {
        constants.push(type.encoding.constantType === 'f32' ? view.getFloat32(start + index * 4, true) : view.getUint32(start + index * 4, true))
      }
      const code: (string | number)[] = []
      const bytes = custom.subarray(start + constCount * 4, start + constCount * 4 + codeLength)
      for (let at = 0; at < bytes.length; at++) {
        const opcode = type.encoding.opcodes.find((entry) => entry.value === bytes[at])
        if (!opcode) {
          diagnostics.push({ severity: 'error', message: `Block ${id}: the formula holds opcode ${bytes[at]}, which the descriptors don't know.` })
          break
        }
        code.push(opcode.alias || opcode.symbol)
        if (opcode.operand !== 'none') code.push(bytes[++at] ?? 0)
      }
      expression = { ...(constants.length ? { constants } : {}), code }
    }

    const enableMode = [...vm.enModes].find(([, value]) => value === raw.enMode)?.[0]?.toLowerCase()
    const onError = [...vm.onErrors].find(([, value]) => value === raw.onError)?.[0]?.toLowerCase()
    blocks.push({
      id,
      type: type.key,
      ...(inputs.length ? { inputs } : {}),
      ...(dynamicInputs.length ? { dynamicInputs } : {}),
      ...(outputs.length ? { outputs } : {}),
      ...(enables.length ? { enables } : {}),
      ...(enables.length > 1 && enableMode === 'all' ? { enableMode: 'all' as const } : {}),
      ...(onError === 'continue' ? { onError: 'continue' as const } : {}),
      ...(raw.eno !== vm.blockNoId ? { eno: true } : {}),
      ...(Object.keys(settings).length ? { settings } : {}),
      ...(expression ? { expression } : {}),
      ...(body !== undefined ? { body } : {}),
    })
  }

  // A FOR's blocks are a span in the frames; on the canvas they are the blocks its ENO gates.
  const positionOf = (id: string) => blocks.findIndex((entry) => entry.id === id)
  const sourcesOf = (block: ProgramBlock): number[] =>
    [...(block.inputs ?? []), ...(block.enables ?? [])].flatMap((path) => (path ? roots(path) : [])).map((root) => BLOCK_PIN.exec(root)?.[1]).filter((id): id is string => id !== undefined).map(positionOf).filter((position) => position >= 0)
  const inside = (position: number, span: { start: number; end: number }) => {
    const wire = known[position]!.raw.wireId
    return wire >= span.start && wire < span.end
  }
  const gated = blocks.map((block, position) => {
    const containing = spans.filter((span) => inside(position, span))
    const sources = sourcesOf(block)
    // A block that reads one from a loop it is outside of would be pulled into that loop when the canvas is compiled.
    for (const span of spans) {
      if (!inside(position, span) && sources.some((source) => inside(source, span))) {
        diagnostics.push({ severity: 'warning', message: `Block ${block.id} reads a block inside loop ${span.id} but runs outside it; on the canvas it will run inside the loop.` })
      }
    }
    const innermost = containing.sort((left, right) => right.start - left.start)[0]
    if (!innermost) return block
    // Depending on another block of the same body already puts it in the loop; the rest need the loop's ENO on EN.
    if (sources.some((source) => inside(source, innermost))) return block
    return { ...block, enables: [...(block.enables ?? []), { root: `${innermost.id}:eno` }] }
  })
  return { ownedPins, blocks: gated }
}

const unsignedByName = (type: VmBlockType, view: DataView, name: string): number | bigint => {
  const field = type.fields.find((entry) => entry.name === name)
  return field ? unsigned(view, field) ?? 0 : 0
}
