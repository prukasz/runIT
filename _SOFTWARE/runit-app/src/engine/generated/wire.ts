import { WIRE_SYMBOLS } from '../wire/definitions'
import type { WireDefinitions } from '../wire/definitions'

/** The shapes of the generated files this adapter reads (generated/common/enums.json, generated/services/contract_vm.json). */
export interface EnumMember {
  readonly name: string
  readonly value: number
  readonly alias?: string
  readonly description?: string
}
export interface EnumTable {
  readonly alias?: string
  readonly members: readonly EnumMember[]
}
export type EnumTables = Readonly<Record<string, EnumTable>>

export interface ContractCommand {
  readonly id: string
  readonly op: number
  readonly symbol?: string
}
export interface VmContract {
  readonly commands: readonly ContractCommand[]
}

const enumValue = (enums: EnumTables, enumName: string, symbol: string): number => {
  const table = enums[enumName]
  if (!table) throw new Error(`generated/common/enums.json has no enum '${enumName}'.`)
  const member = table.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`Enum '${enumName}' has no member '${symbol}' (generated/common/enums.json).`)
  return member.value
}

const pick = <K extends string>(enums: EnumTables, enumName: string, symbols: Readonly<Record<K, string>>): Record<K, number> =>
  Object.fromEntries(Object.entries<string>(symbols).map(([key, symbol]) => [key, enumValue(enums, enumName, symbol)])) as Record<K, number>

/**
 * The wire numbers from the generated descriptors, by the C symbol each short name stands for (WIRE_SYMBOLS). A symbol the
 * firmware no longer publishes stops the load with a message that names it, instead of a wrong number reaching the board.
 */
export const wireDefinitions = (enums: EnumTables, vmContract: VmContract): WireDefinitions => {
  const op = (id: string): number => {
    const command = vmContract.commands.find((c) => c.id === id)
    if (!command) throw new Error(`generated/services/contract_vm.json has no command '${id}'.`)
    return command.op
  }
  return {
    routerKey: enumValue(enums, WIRE_SYMBOLS.routerKey.enum, WIRE_SYMBOLS.routerKey.symbol),
    ops: Object.fromEntries(Object.entries(WIRE_SYMBOLS.ops).map(([key, id]) => [key, op(id)])) as WireDefinitions['ops'],
    objTypes: pick(enums, WIRE_SYMBOLS.objTypes.enum, WIRE_SYMBOLS.objTypes.symbols),
    headFlags: pick(enums, WIRE_SYMBOLS.headFlags.enum, WIRE_SYMBOLS.headFlags.symbols),
    enMode: pick(enums, WIRE_SYMBOLS.enMode.enum, WIRE_SYMBOLS.enMode.symbols),
    onError: pick(enums, WIRE_SYMBOLS.onError.enum, WIRE_SYMBOLS.onError.symbols),
    idxKinds: pick(enums, WIRE_SYMBOLS.idxKinds.enum, WIRE_SYMBOLS.idxKinds.symbols),
    exec: pick(enums, WIRE_SYMBOLS.exec.enum, WIRE_SYMBOLS.exec.symbols),
  }
}
