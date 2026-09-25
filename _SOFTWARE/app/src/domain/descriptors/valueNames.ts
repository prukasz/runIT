import type { CommandCatalog } from './commandCatalog'
import type { ErrorCatalog } from './errorCatalog'
import type { GeneratedBoardFile, GeneratedEnumsFile, GeneratedVmBlocksIndex } from './generatedTypes'

/** A field's naming annotation, as published in the error catalog (`enum_ref` / `id`). */
export interface NamedField {
  readonly enumRef?: string
  readonly idRef?: { readonly kind: string; readonly parentField?: string }
}

/**
 * Names a firmware value from the catalog its annotation points at: an enum
 * member, a board device, an error tag / owner / level, a command class or
 * packet, a VM block type, an esp_err_t code.
 */
export interface ValueNames {
  /**
   * The name of `value`, or undefined when nothing names it (no annotation, or
   * an ID the catalogs don't know). `siblings` are the other fields of the same
   * payload (an `rx-packet` reads its class byte, a `contract-feature` its contract type from one).
   */
  name(field: NamedField, value: number, siblings?: Readonly<Record<string, unknown>>): string | undefined
  /** Every `id` kind this resolver can name (the error catalog's `id_kinds` must be a subset). */
  readonly idKinds: ReadonlySet<string>
}

export interface ValueNameSources {
  readonly enums: GeneratedEnumsFile
  readonly board: GeneratedBoardFile
  readonly blocks: GeneratedVmBlocksIndex
  readonly errors: ErrorCatalog
  readonly commands: CommandCatalog
}

export const buildValueNames = (sources: ValueNameSources): ValueNames => {
  const devices = new Map(sources.board.devices.map((device) => [device.id, device.name]))
  const blocks = new Map(sources.blocks.blocks.map((block) => [block.id, block.title]))
  const classes = new Map(sources.commands.groups.map((group) => [group.classHeader, group.title]))
  const packets = new Map(sources.commands.commands.map((command) => [(command.classHeader << 8) | command.packetHeader, command.name]))

  const byKind: Readonly<Record<string, (value: number, parent: number | undefined) => string | undefined>> = {
    device: (value) => devices.get(value),
    'error-tag': (value) => sources.errors.tag(value)?.name,
    'error-owner': (value) => sources.errors.owner(value)?.name,
    'error-level': (value) => sources.errors.level(value)?.alias,
    'rx-class': (value) => classes.get(value),
    'rx-packet': (value, classByte) => (classByte === undefined ? undefined : packets.get((classByte << 8) | value)),
    'contract-feature': (value, contract) => (contract === undefined ? undefined : sources.errors.contractFeature(contract, value)),
    'vm-block-type': (value) => blocks.get(value),
    'esp-err': (value) => sources.errors.espError(value),
  }

  return {
    idKinds: new Set(Object.keys(byKind)),
    name: (field, value, siblings = {}) => {
      if (field.enumRef !== undefined) {
        const member = sources.enums.enums[field.enumRef]?.members.find((entry) => entry.value === value)
        return member ? member.alias ?? member.name : undefined
      }
      if (field.idRef !== undefined) {
        const parent = field.idRef.parentField === undefined ? undefined : siblings[field.idRef.parentField]
        return byKind[field.idRef.kind]?.(value, typeof parent === 'number' ? parent : undefined)
      }
      return undefined
    },
  }
}
