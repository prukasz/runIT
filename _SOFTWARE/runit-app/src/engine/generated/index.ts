import enumsJson from '@generated/common/enums.json'
import limitsJson from '@generated/common/limits.json'
import vmContractJson from '@generated/services/contract_vm.json'
import type { GeneratedInput } from '../catalog/fromGenerated'
import { limitsFromGenerated } from './limits'
import type { GeneratedLimits } from './limits'
import { wireDefinitions } from './wire'
import type { EnumTables, VmContract } from './wire'

/*
 * The one place the engine touches generated/ (the bundle `templates/generate_bundle.py` copies from components/). Everything
 * else takes parsed definitions as arguments, so tests and other apps can supply their own.
 */

export type { ContractCommand, EnumMember, EnumTable, EnumTables, VmContract } from './wire'
export { wireDefinitions } from './wire'
export { limitsFromGenerated } from './limits'
export type { GeneratedLimits } from './limits'

/** The firmware's enums as generated/common/enums.json publishes them. */
export const generatedEnums = (enumsJson as unknown as { enums: EnumTables }).enums

/** The wire numbers of the bundled firmware descriptors. */
export const loadWireDefinitions = () => wireDefinitions(generatedEnums, vmContractJson as unknown as VmContract)

/** The limits of the bundled firmware configuration (generated/common/limits.json: the `#@limit` options of the Kconfig). */
export const loadLimits = () => limitsFromGenerated(limitsJson as unknown as GeneratedLimits)

/*
 * The descriptor files themselves. Vite resolves the globs at build time, so a new `block_*.json` / `contract_*.json` /
 * `device_*.json` in generated/ is picked up with no list to edit (the same rule as templates/generate_bundle.py).
 */
const files = <T>(modules: Record<string, unknown>): readonly T[] => Object.keys(modules).sort().map((path) => modules[path] as T)

/** Every block, contract and device descriptor of the bundle, with the enums, ready for `Catalog.fromGenerated`. */
export const loadGeneratedInput = (): GeneratedInput => ({
  blocks: files<GeneratedInput['blocks'][number]>(import.meta.glob('@generated/blocks/block_*.json', { eager: true, import: 'default' })),
  contracts: files<GeneratedInput['contracts'][number]>(import.meta.glob('@generated/contracts/contract_*.json', { eager: true, import: 'default' })),
  devices: files<GeneratedInput['devices'][number]>(import.meta.glob('@generated/devices/device_*.json', { eager: true, import: 'default' })),
  enums: generatedEnums,
})
