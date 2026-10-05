import type { DeviceType } from '../descriptors'

/** A device value read for one device: what the number is called there, its unit and range, and the hint that says it (`-45 to 45 deg`). */
export interface DeviceValue {
  readonly title: string
  readonly unit?: string
  readonly min?: number
  readonly max?: number
  readonly hint: string
}

const shown = (value: number): string => String(Math.round(value * 100) / 100)

/**
 * The variant of the device's value `name` that holds for its install settings (the first whose `@when` pairs all match), with
 * range bounds that name install fields filled in. Undefined when the device has no such value or no variant holds.
 */
export const resolveDeviceValue = (type: DeviceType | undefined, install: Readonly<Record<string, number>>, name: string): DeviceValue | undefined => {
  const variant = type?.values[name]?.find((entry) => entry.when.every((condition) => install[condition.field] === condition.value))
  if (!variant) return undefined
  const bound = (value: number | { readonly field: string } | undefined) => (typeof value === 'object' ? install[value.field] : value)
  const min = bound(variant.range?.min)
  const max = bound(variant.range?.max)
  const range = min !== undefined && max !== undefined ? `${shown(min)} to ${shown(max)}` : ''
  return {
    title: variant.title,
    ...(variant.unit ? { unit: variant.unit } : {}),
    ...(min !== undefined ? { min } : {}),
    ...(max !== undefined ? { max } : {}),
    hint: [range, variant.unit].filter(Boolean).join(' '),
  }
}
