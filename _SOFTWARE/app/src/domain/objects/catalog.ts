import programJson from '@data-structures/vm/vm-program.generated.json'
import modelJson from '@data-structures/vm/vm-model.generated.json'

/**
 * The object types and limits of the VM, read from the generated descriptors (data-structures/vm) so a change in the
 * firmware reaches the editor on the next regeneration.
 */

export interface VmObjectType {
  /** The type's key without the `VM_OBJ_` prefix: `U8`, `U32`, `I32`, `F`, `B`, `STR`, `PTR`. */
  readonly key: string
  readonly alias: string
  /** Bytes of one element in the board's memory (what `payload_size` counts). */
  readonly memoryWidth: number
  readonly wireType: string
}

interface ProgramJson {
  readonly types: readonly { symbol: string; alias: string; memory_width: number; wire_type: string }[]
  readonly constants: Readonly<Record<string, { value: number }>>
}
interface ModelJson {
  readonly structures: readonly { name: string; fields: readonly { name: string; c_type: string }[] }[]
}

const program = programJson as unknown as ProgramJson
const model = modelJson as unknown as ModelJson

const UNSIGNED_MAX: Readonly<Record<string, number>> = { uint8_t: 0xff, uint16_t: 0xffff, uint32_t: 0xffffffff }

const need = <T>(value: T | undefined, what: string): T => {
  if (value === undefined) throw new Error(`The VM descriptors have no ${what} (data-structures/vm).`)
  return value
}

const types: readonly VmObjectType[] = program.types.map((entry) => ({
  key: entry.symbol.replace(/^VM_OBJ_/, ''),
  alias: entry.alias,
  memoryWidth: entry.memory_width,
  wireType: entry.wire_type,
}))

const payloadType = need(model.structures.find((s) => s.name === 'vm_obj_head_t')?.fields.find((f) => f.name === 'payload_size')?.c_type, 'vm_obj_head_t.payload_size')

/** The value types a user object can have (every type but the folder's PTR). */
export const valueTypes: readonly VmObjectType[] = types.filter((type) => type.key !== 'PTR')

/** Longest object name, in bytes. */
export const NAME_MAX = need(program.constants.VM_OBJ_NAME_MAX?.value, 'VM_OBJ_NAME_MAX')

/** Largest payload of one object, in bytes. */
export const PAYLOAD_MAX = need(UNSIGNED_MAX[payloadType], `the range of ${payloadType}`)

export const typeOf = (key: string): VmObjectType | undefined => types.find((type) => type.key === key)

/** Most elements an object of this type holds. */
export const maxElementsOf = (key: string): number => {
  const type = typeOf(key)
  return type ? Math.floor(PAYLOAD_MAX / type.memoryWidth) : PAYLOAD_MAX
}
