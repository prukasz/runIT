import contractsJson from '@data-structures/contracts/contracts.generated.json'
import enumsJson from '@data-structures/enums.json'

/**
 * The firmware's numbers, read from the generated descriptors (data-structures/enums.json and contracts/contracts.generated.json)
 * by their C symbol. The app names a symbol (`VM_OP_RESET`), never a number, so a renumbering in the firmware reaches the app on the
 * next regeneration; a symbol that no longer exists stops the app at load with a message that names it.
 */

export interface EnumMember {
  readonly name: string
  readonly value: number
  readonly alias: string
  readonly description?: string
  /** Bytes of one element on the wire / in the board's memory (`vm_obj_t_e`). */
  readonly wire_size?: number
  readonly mem_size?: number
}

interface EnumEntry {
  readonly alias: string
  readonly members: readonly EnumMember[]
}

interface RouterEntry {
  readonly key: number
  readonly symbol: string
  readonly ops: readonly { readonly id: number; readonly symbol: string }[]
}

const ENUMS = (enumsJson as unknown as { enums: Record<string, EnumEntry> }).enums
const ROUTERS = (contractsJson as unknown as { routers: RouterEntry[] }).routers

/** The members of a published enum or `//#ref-defines` group. */
export const enumMembers = (enumName: string): readonly EnumMember[] => {
  const entry = ENUMS[enumName]
  if (!entry) throw new Error(`The firmware publishes no enum '${enumName}' (data-structures/enums.json).`)
  return entry.members
}

/** One member by its C name. */
export const member = (enumName: string, symbol: string): EnumMember => {
  const found = enumMembers(enumName).find((m) => m.name === symbol)
  if (!found) throw new Error(`${enumName} has no member '${symbol}' (data-structures/enums.json).`)
  return found
}

/** The value of one member by its C name. */
export const enumValue = (enumName: string, symbol: string): number => member(enumName, symbol).value

/** The name of the member with `value`, or undefined. */
export const enumName = (enumName_: string, value: number): string | undefined => enumMembers(enumName_).find((m) => m.value === value)?.name

/** A root key by its symbol (`ROUTER_KEY_VM`). */
export const routerKey = (symbol: string): number => enumValue('router_key', symbol)

/** An operation id of a router by the operation's symbol (`routerOp('ROUTER_KEY_VM', 'VM_OP_RESET')`). */
export const routerOp = (routerSymbol: string, opSymbol: string): number => {
  const router = ROUTERS.find((r) => r.symbol === routerSymbol)
  if (!router) throw new Error(`The firmware has no router '${routerSymbol}' (data-structures/contracts/contracts.generated.json).`)
  const op = router.ops.find((o) => o.symbol === opSymbol)
  if (!op) throw new Error(`${routerSymbol} has no operation '${opSymbol}' (data-structures/contracts/contracts.generated.json).`)
  return op.id
}

/** `{ short: value }` of the named members: `values('vm_exec_command_e', { scan: 'VM_EXEC_SCAN_MODE' })`. */
export const values = <K extends string>(enumName_: string, symbols: Readonly<Record<K, string>>): Readonly<Record<K, number>> =>
  Object.fromEntries(Object.entries<string>(symbols).map(([key, symbol]) => [key, enumValue(enumName_, symbol)])) as Record<K, number>
