import enumsJson from '@generated/common/enums.json'
import limitsJson from '@generated/common/limits.json'
import type { GeneratedInput } from '../catalog/fromGenerated'
import type { WireDefinitions } from '../wire/format/definitions'
import { frameMaxFromGenerated, limitsFromGenerated } from './limits'
import type { GeneratedLimits } from './limits'
import { wireDefinitions } from './wire'
import type { EnumTables, VmContract } from './wire'

/*
 * The one place the engine touches generated/ (the bundle `templates/generate_bundle.py` copies from runit-esp/components/). Everything
 * else takes parsed definitions as arguments, so tests and other apps can supply their own.
 */

export type { ContractCommand, EnumMember, EnumTable, EnumTables, VmContract } from './wire'
export { wireDefinitions } from './wire'
export { frameMaxFromGenerated, limitsFromGenerated } from './limits'
export type { GeneratedLimits } from './limits'

/** The firmware's enums as generated/common/enums.json publishes them. */
export const generatedEnums = (enumsJson as unknown as { enums: EnumTables }).enums

/** The VM's contract, when the firmware publishes one (the rework's firmware has no VM yet: none). */
const vmContracts = Object.values(import.meta.glob('@generated/services/contract_vm.json', { eager: true, import: 'default' })) as VmContract[]

/** Whether the firmware's descriptors have a VM: without it the engine cannot make a wire frame. */
export const vmPublished = (): boolean => vmContracts.length > 0

/** The wire numbers of the bundled firmware descriptors; undefined without a VM: the engine then checks a program but builds no frames. */
export const loadWireDefinitions = (): WireDefinitions | undefined => {
  const contract = vmContracts[0]
  return contract ? wireDefinitions(generatedEnums, contract) : undefined
}

/**
 * The limits of the bundled firmware configuration (generated/common/limits.json: the `#@limit` options of the Kconfig) and
 * the frame size (cmd.h, generated/common/enums.json).
 */
export const loadLimits = () => ({ ...limitsFromGenerated(limitsJson as unknown as GeneratedLimits), frameMax: frameMaxFromGenerated(generatedEnums) })

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
