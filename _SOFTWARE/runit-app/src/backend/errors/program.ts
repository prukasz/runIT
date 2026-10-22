import type { CompileResult } from '@engine/compiler'
import type { Project, Variable } from '@engine/model'
import { bundledBlockTypes, bundledEnums } from '../definitions'
import type { Ref, RefResolver, RefTarget } from './interpret'

/*
 * The app's side of an error's references: what a number on the board is in the project the user edits. The board numbers
 * objects, accessors and blocks of the loaded program (`vm_object`, `vm_accessor`, `vm_block` and the pins of a block); the
 * compile result that produced the program maps them back to variables and block instances. Devices are the app's own
 * (the project's devices by ID). Labels are names, never an internal id.
 */

/** What an error points at in the app. */
export type AppTarget =
  | { readonly kind: 'device'; readonly device: number }
  | { readonly kind: 'pin'; readonly device: number; readonly pin: number }
  | { readonly kind: 'channel'; readonly device: number; readonly channel: number }
  | { readonly kind: 'variable'; readonly variable: string }
  | { readonly kind: 'block'; readonly block: string; readonly pin?: { readonly kind: 'input' | 'output' | 'enable' | 'eno'; readonly index: number } }

/** The program's references: `result` is the compile result of `project` that is loaded on the board. */
export const programRefs = (project: Project, result: Pick<CompileResult, 'ids' | 'ir'>, blockTypes: ReadonlyMap<number, string> = bundledBlockTypes()): RefResolver => {
  // variable paths: the names from the top folder down
  const paths = new Map<string, string>()
  const walk = (variables: readonly Variable[], prefix: string) => {
    for (const v of variables) {
      const path = prefix ? `${prefix}.${v.name}` : v.name
      paths.set(v.id, path)
      if (v.kind === 'folder') walk(v.children, path)
    }
  }
  walk(project.variables, '')

  const variableOf = new Map([...result.ids.variables].map(([variable, object]) => [object, variable]))
  const blockOf = new Map([...result.ids.blocks].map(([block, id]) => [id, block]))
  const outputOf = new Map<number, { block: string; kind: 'output' | 'eno'; index: number }>()
  for (const [key, object] of result.ids.outputs) {
    const m = /^(.*):(?:out(\d+)|(eno))$/.exec(key)
    if (m) outputOf.set(object, { block: m[1]!, kind: m[3] ? 'eno' : 'output', index: Number(m[2] ?? 0) })
  }
  const instances = new Map(project.blocks.map((b) => [b.id, b]))
  const irBlocks = new Map((result.ir?.blocks ?? []).map((b) => [b.id, b]))

  const blockLabel = (id: number): string | undefined => {
    const instance = instances.get(blockOf.get(id) ?? '')
    if (!instance) return undefined
    if (instance.name) return instance.name
    const type = irBlocks.get(id)?.type
    return `${(type !== undefined && blockTypes.get(type)) || instance.type} ${id + 1}`
  }
  const pinLabel = (kind: 'input' | 'output' | 'enable' | 'eno', index: number) => (kind === 'eno' ? 'ENO' : `${kind} ${index + 1}`)
  const block = (id: number, pin?: { kind: 'input' | 'output' | 'enable' | 'eno'; index: number }): RefTarget | undefined => {
    const instance = blockOf.get(id)
    const label = blockLabel(id)
    if (instance === undefined || label === undefined) return undefined
    return { label: pin ? `${pinLabel(pin.kind, pin.index)} of ${label}` : label, target: { kind: 'block', block: instance, ...(pin ? { pin } : {}) } satisfies AppTarget }
  }
  const object = (id: number): RefTarget | undefined => {
    const variable = variableOf.get(id)
    if (variable !== undefined) return { label: paths.get(variable) ?? variable, target: { kind: 'variable', variable } satisfies AppTarget }
    const output = outputOf.get(id)
    if (output) {
      const board = result.ids.blocks.get(output.block)
      return board === undefined ? undefined : block(board, { kind: output.kind, index: output.index })
    }
    const name = result.ir?.objects[id]?.name
    return name ? { label: name } : undefined
  }
  const accessor = (id: number): RefTarget | undefined => {
    for (const b of result.ir?.blocks ?? []) {
      const input = b.inputs.indexOf(id)
      if (input >= 0) return block(b.id, { kind: 'input', index: input })
      const enable = b.enables.indexOf(id)
      if (enable >= 0) return block(b.id, { kind: 'enable', index: enable })
    }
    const root = result.ir?.accessors.find((a) => a.id === id)?.root
    return root === undefined ? undefined : object(root)
  }
  // a pin of a block the error names: the template says whose (`{pin_id} of {block_idx}`)
  const pin = (ref: Ref, kind: 'input' | 'output' | 'enable'): RefTarget | undefined => {
    const target = ref.scope?.kind === 'vm_block' ? block(ref.scope.id, { kind, index: ref.id }) : undefined
    return target && { label: pinLabel(kind, ref.id), target: target.target }
  }

  return (ref) => {
    switch (ref.kind) {
      case 'vm_object': return object(ref.id)
      case 'vm_accessor': return accessor(ref.id)
      case 'vm_block': return block(ref.id)
      case 'vm_block_input': return pin(ref, 'input')
      case 'vm_block_output': return pin(ref, 'output')
      case 'vm_block_enable': return pin(ref, 'enable')
      default: return undefined
    }
  }
}

/** `SYS_DEVICE_ID_NONE` (enums.json, group `device_id`): no device, the source of the board's own events (the power manager, a sweep). */
const NO_DEVICE = bundledEnums().device_id?.members.find((m) => m.name === 'SYS_DEVICE_ID_NONE')?.value

/**
 * The app's devices by ID -> name: devices, and the pins and channels of a device the error names (named `Pin 5` by errors.json).
 * No device (`SYS_DEVICE_ID_NONE`) is the board itself.
 */
export const deviceRefs = (devices: ReadonlyMap<number, string>): RefResolver => (ref) => {
  if (ref.kind === 'device' && ref.id === NO_DEVICE) return { label: 'the board' }
  if (ref.kind === 'device') {
    const name = devices.get(ref.id)
    return name === undefined ? undefined : { label: name, target: { kind: 'device', device: ref.id } satisfies AppTarget }
  }
  if ((ref.kind === 'pin' || ref.kind === 'channel') && ref.scope?.kind === 'device' && devices.has(ref.scope.id)) {
    const target: AppTarget = ref.kind === 'pin' ? { kind: 'pin', device: ref.scope.id, pin: ref.id } : { kind: 'channel', device: ref.scope.id, channel: ref.id }
    return { target }
  }
  return undefined
}
