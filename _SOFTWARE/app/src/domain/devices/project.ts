import { BOARD_DEVICES, BOARD_PIN_LINKS, deviceType, entryRequiredModes, fieldChoices, fieldDefault, MAX_DEVICE_ID } from './catalog'
import type { DeviceField, DeviceType, PinUser } from './catalog'

/*
 * The user's devices in the project: a device type (its descriptor id), the device ID it is created under, a name, and the values of
 * its create struct. The values are kept flat by a key per number of the struct: `full_scale_mV`, a pin of another device as
 * `oe_pin.device` / `oe_pin.pin`, an array element as `pin[3].device` or `current_limit_mA[2]`, so a field added in C is just one
 * more key and an old project still opens (a missing key takes the field's default).
 */

export interface ProjectDevice {
  readonly id: string
  /** The device type: its descriptor id (`device_servo`). */
  readonly type: string
  /** The ID the board creates it under. */
  readonly deviceId: number
  readonly name: string
  readonly description?: string
  readonly tags: readonly string[]
  /** The create struct's numbers, by key (see above). */
  readonly install: Readonly<Record<string, number>>
}

/** No device: a pin left unwired (`SYS_DEVICE_ID_NONE`). */
export const NO_DEVICE = 0xffff

/** One number the install form shows, or one pin (its device and pin, as one row). */
export type InstallEntry =
  | { readonly kind: 'number'; readonly key: string; readonly field: DeviceField; readonly label: string }
  | { readonly kind: 'pin'; readonly key: string; readonly field: DeviceField; readonly label: string }

const isPin = (field: DeviceField): boolean => !!field.pin_ref || field.struct === 'sys_io_pin_t'
const label = (field: DeviceField): string => field.alias ?? field.name

/** The rows of a type's install form, in struct order. */
export const installEntries = (type: DeviceType | undefined): InstallEntry[] => {
  const out: InstallEntry[] = []
  const walk = (fields: readonly DeviceField[], prefix: string, labelPrefix: string) => {
    for (const field of fields) {
      const key = `${prefix}${field.name}`
      const name = `${labelPrefix}${label(field)}`
      const count = field.array_len ?? 0
      if (count > 0) {
        for (let index = 0; index < count; index++) {
          const elementModes = Array.isArray(field.modes) && Array.isArray((field.modes as unknown[])[index])
            ? (field.modes as unknown as readonly unknown[][])[index]
            : field.modes
          const element = {
            ...field,
            array_len: undefined,
            ...(elementModes !== undefined ? { modes: elementModes as any } : {}),
          }
          const elementLabel = field.aliases?.[index] ?? `${name} ${index + 1}`
          if (isPin(field)) out.push({ kind: 'pin', key: `${key}[${index}]`, field: element, label: elementLabel })
          else if (field.fields?.length) walk(field.fields, `${key}[${index}].`, `${elementLabel} `)
          else out.push({ kind: 'number', key: `${key}[${index}]`, field: element, label: elementLabel })
        }
      } else if (isPin(field)) out.push({ kind: 'pin', key, field, label: name })
      else if (field.fields?.length) walk(field.fields, `${key}.`, `${name} `)
      else out.push({ kind: 'number', key, field, label: name })
    }
  }
  walk(type?.create?.fields ?? [], '', '')
  return out
}

/** A new device's values: every field at its default, every pin unwired. */
export const defaultInstall = (type: DeviceType | undefined): Record<string, number> => {
  const values: Record<string, number> = {}
  for (const entry of installEntries(type)) {
    if (entry.kind === 'pin') {
      values[`${entry.key}.device`] = NO_DEVICE
      values[`${entry.key}.pin`] = 0
    } else values[entry.key] = fieldDefault(entry.field)
  }
  return values
}

/** Whether a value is one the field takes: a listed choice, or inside its limits and its size. */
export const installValueOk = (field: DeviceField, value: number): boolean => {
  const choices = fieldChoices(field)
  if (choices) return choices.some((choice) => choice.value === value)
  const bits = field.size * 8
  const signed = field.type.startsWith('i')
  const low = signed ? -(2 ** (bits - 1)) : 0
  const high = signed ? 2 ** (bits - 1) - 1 : 2 ** bits - 1
  return Number.isInteger(value) && value >= Math.max(low, field.min ?? low) && value <= Math.min(high, field.max ?? high)
}

/** The first device ID above every board device and every device the project already has. */
export const nextDeviceId = (devices: readonly Pick<ProjectDevice, 'deviceId'>[]): number => {
  const used = new Set([...BOARD_DEVICES.map((device) => device.id), ...devices.map((device) => device.deviceId)])
  let id = Math.max(...BOARD_DEVICES.map((device) => device.id), -1) + 1
  while (used.has(id) && id < MAX_DEVICE_ID) id++
  return id
}

export const newDevice = (typeId: string, devices: readonly ProjectDevice[]): ProjectDevice => {
  const type = deviceType(typeId)
  const sameType = devices.filter((device) => device.type === typeId).length
  return {
    id: `dev-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`,
    type: typeId,
    deviceId: nextDeviceId(devices),
    name: `${type?.alias ?? typeId}${sameType ? ` ${sameType + 1}` : ''}`,
    tags: [],
    install: defaultInstall(type),
  }
}

/** What is wrong with the devices: two devices on one ID (a board device's included), a value a field does not take. */
export const deviceProblems = (devices: readonly ProjectDevice[]): { readonly deviceId: string; readonly message: string }[] => {
  const problems: { deviceId: string; message: string }[] = []
  for (const device of devices) {
    const board = BOARD_DEVICES.find((entry) => entry.id === device.deviceId)
    if (board) problems.push({ deviceId: device.id, message: `Device ID ${device.deviceId} is the board's ${board.alias}.` })
    const twin = devices.find((other) => other !== device && other.deviceId === device.deviceId)
    if (twin) problems.push({ deviceId: device.id, message: `Device ID ${device.deviceId} is also ${twin.name}'s.` })
    for (const entry of installEntries(deviceType(device.type))) {
      if (entry.kind !== 'number') {
        if (entry.field.required && (device.install[`${entry.key}.device`] ?? NO_DEVICE) === NO_DEVICE) problems.push({ deviceId: device.id, message: `${entry.label} must be wired to a pin.` })
        continue
      }
      const value = device.install[entry.key]
      if (value !== undefined && !installValueOk(entry.field, value)) problems.push({ deviceId: device.id, message: `${entry.label}: ${value} is not a value it takes.` })
    }
  }
  return problems
}

const isRecord = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value)

/** Saved devices: what can be read is kept, a missing value takes its field's default, a device of an unknown type is kept as it is. */
export const devicesFromJson = (value: unknown): ProjectDevice[] => {
  if (!Array.isArray(value)) return []
  return value.flatMap((raw): ProjectDevice[] => {
    if (!isRecord(raw) || typeof raw.id !== 'string' || typeof raw.type !== 'string' || typeof raw.deviceId !== 'number') return []
    const install = isRecord(raw.install) ? Object.fromEntries(Object.entries(raw.install).filter((entry): entry is [string, number] => typeof entry[1] === 'number')) : {}
    return [{
      id: raw.id,
      type: raw.type,
      deviceId: raw.deviceId,
      name: typeof raw.name === 'string' ? raw.name : raw.type,
      ...(typeof raw.description === 'string' && raw.description ? { description: raw.description } : {}),
      tags: Array.isArray(raw.tags) ? raw.tags.filter((tag): tag is string => typeof tag === 'string') : [],
      install: { ...defaultInstall(deviceType(raw.type)), ...install },
    }]
  })
}

export const pinKey = (deviceId: number, pin: number): string => `${deviceId}:${pin}`

/** Every pin taken on the board: by the fixed onboard links and by the user's devices in the project. */
export const pinUsers = (devices: readonly ProjectDevice[]): Map<string, PinUser[]> => {
  const users = new Map<string, PinUser[]>()
  const add = (targetDevice: number, targetPin: number, user: PinUser) => {
    const key = pinKey(targetDevice, targetPin)
    users.set(key, [...(users.get(key) ?? []), user])
  }

  for (const link of BOARD_PIN_LINKS) {
    add(link.targetDevice, link.targetPin, link)
  }

  for (const device of devices) {
    const type = deviceType(device.type)
    for (const entry of installEntries(type)) {
      if (entry.kind !== 'pin') continue
      const targetDevice = device.install[`${entry.key}.device`]
      const targetPin = device.install[`${entry.key}.pin`]
      if (targetDevice !== undefined && targetDevice !== NO_DEVICE && targetPin !== undefined) {
        const reqModes = entryRequiredModes(entry.field)
        const mode = reqModes.length === 1 ? reqModes[0] : undefined
        add(targetDevice, targetPin, {
          ownerRef: device.id,
          ownerName: device.name,
          targetDevice,
          targetPin,
          use: entry.label,
          ...(mode !== undefined ? { mode } : {}),
        })
      }
    }
  }

  return users
}
