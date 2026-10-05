import type { VmBlockField, VmBlockType } from '../../domain/descriptors'
import type { CanvasBlock } from '../../domain/project'

/*
 * Settings chosen from an event catalog (`@source-field`): a device, then its channel and event, all narrowed by the event domain
 * of another setting, each with a wildcard ("any") the setting starts at.
 */

/** The settings that stop making sense when `name` changes: the ones narrowed by it, and by those. */
export const sourceDependents = (type: VmBlockType, name: string): readonly VmBlockField[] => {
  const found: VmBlockField[] = []
  const pending = [name]
  while (pending.length) {
    const current = pending.pop()!
    for (const field of type.fields) if ((field.sourceField === current || (field.sourceField && field.deviceField === current)) && !found.includes(field)) { found.push(field); pending.push(field.name) }
  }
  return found
}

/** A setting without its dependents, which fall back to any. */
export const withoutSettings = (block: CanvasBlock, names: readonly string[]): CanvasBlock => {
  const settings = Object.fromEntries(Object.entries(block.settings ?? {}).filter(([name]) => !names.includes(name)))
  const { settings: _, ...rest } = block
  return Object.keys(settings).length ? { ...rest, settings } : rest
}

/** An enum setting as its member's value: by value, name or name without its prefix; undefined while it is unset or the wildcard. */
export const enumSettingValue = (type: VmBlockType, field: VmBlockField, raw: number | string | undefined): number | undefined => {
  if (raw === undefined || raw === field.anyValue) return undefined
  const members = type.enums.get(field.enumRef ?? '') ?? []
  return typeof raw === 'number' ? raw : members.find((member) => member.name === raw || member.name.endsWith(`_${raw}`))?.value
}
