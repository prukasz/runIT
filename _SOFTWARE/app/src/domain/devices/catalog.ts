import boardJson from '@data-structures/board/board.generated.json'
import { enumMembers } from '../../backend/catalog'

/*
 * The devices as the firmware publishes them, nothing written in the app:
 *   - the device types: data-structures/devices/<id>.generated.json (from each driver's `//#device` record and create struct), with
 *     the user guide next to it (<id>.md, copied from the driver's folder);
 *   - the board's own devices: data-structures/board/board.generated.json (the `board_device` group and the create calls in C).
 */

/** A field of a create struct or of an op's arguments, as the descriptor publishes it. */
export interface DeviceField {
  readonly name: string
  readonly alias?: string
  readonly description?: string
  readonly type: string
  readonly offset: number
  readonly size: number
  readonly unit?: string
  readonly min?: number
  readonly max?: number
  readonly default?: number | { readonly value: number; readonly alias?: string }
  readonly enum_ref?: string
  readonly choices?: readonly { readonly value: number; readonly alias: string }[]
  readonly one_of?: readonly number[]
  readonly required?: boolean
  /** A pin of another device (`sys_io_pin_t`): its device and pin are the two sub-fields. */
  readonly pin_ref?: boolean
  readonly modes?: readonly { readonly value: number; readonly alias: string }[]
  readonly default_mode?: number
  readonly id_kind?: string
  readonly any_value?: number
  readonly array_len?: number
  readonly aliases?: readonly string[]
  readonly struct?: string
  readonly fields?: readonly DeviceField[]
}

/** What one device type narrows in a contract's argument (the pins it has, its limits). */
export interface DeviceParam {
  readonly field: string
  readonly alias?: string
  readonly one_of?: readonly number[]
  readonly min?: number
  readonly max?: number
  readonly default?: number
  readonly note?: string
  readonly device_wide?: boolean
}

export interface DeviceOp {
  readonly symbol: string
  readonly id: number
  readonly alias: string
  readonly description?: string
  readonly args?: { readonly struct: string; readonly size: number; readonly fields: readonly DeviceField[] }
  readonly params?: readonly DeviceParam[]
  readonly returns?: unknown
}

export interface DeviceContract {
  readonly contract: string
  readonly id: number
  readonly alias: string
  readonly ops: readonly DeviceOp[]
}

export interface DeviceType {
  /** The descriptor id (`device_pca9685`). */
  readonly id: string
  readonly alias: string
  readonly description: string
  readonly tags: readonly string[]
  readonly protocols: readonly string[]
  readonly datasheet?: string
  /** The firmware's type (`SYS_DEVICE_TYPE_PCA9685`, 0x0048 …). */
  readonly type: { readonly symbol: string; readonly id: number }
  /** The CREATE arguments: absent for a type the board creates by itself only. */
  readonly create?: { readonly struct: string; readonly size: number; readonly fields: readonly DeviceField[] }
  readonly contracts: readonly DeviceContract[]
  readonly own_ops: readonly DeviceOp[]
  readonly events?: readonly { readonly name?: string; readonly alias: string; readonly description?: string }[]
  /** The user guide (Markdown), when the driver has one. */
  readonly guide?: string
}

export interface BoardDevice {
  readonly id: number
  readonly symbol: string
  readonly alias: string
  readonly description?: string
  readonly type?: DeviceType
}

const files = import.meta.glob('@data-structures/devices/*.generated.json', { eager: true, import: 'default' }) as Record<string, Omit<DeviceType, 'guide'> & { guide?: string }>
const guides = import.meta.glob('@data-structures/devices/*.md', { eager: true, query: '?raw', import: 'default' }) as Record<string, string>
const guideText = (name: string | undefined): string | undefined => {
  if (!name) return undefined
  const entry = Object.entries(guides).find(([path]) => path.endsWith(`/${name}`))
  return entry?.[1]
}

/** Every device type the firmware has, by alias. */
export const DEVICE_TYPES: readonly DeviceType[] = Object.values(files)
  .map((raw) => ({ ...raw, tags: raw.tags ?? [], protocols: raw.protocols ?? [], contracts: raw.contracts ?? [], own_ops: raw.own_ops ?? [], guide: guideText(raw.guide) }))
  .sort((a, b) => a.alias.localeCompare(b.alias))

export const deviceType = (id: string | undefined): DeviceType | undefined => DEVICE_TYPES.find((type) => type.id === id)

/** The board's own devices, in ID order. */
export const BOARD_DEVICES: readonly BoardDevice[] = (boardJson as unknown as { devices: { id: number; symbol: string; alias: string; description?: string; descriptor: string }[] }).devices.map((device) => ({
  id: device.id,
  symbol: device.symbol,
  alias: device.alias,
  ...(device.description ? { description: device.description } : {}),
  ...(deviceType(device.descriptor) ? { type: deviceType(device.descriptor)! } : {}),
}))

/** The highest device ID (`SYS_DEVICE_ID_NONE` = 0xFFFF is "no device"). */
export const MAX_DEVICE_ID = 0xfffe

/** The choices of a field: its `@choices`, the members of its enum, or the values it may take (`@one-of`); none = a number. */
export const fieldChoices = (field: DeviceField): readonly { readonly value: number; readonly alias: string }[] | undefined => {
  if (field.choices?.length) return field.choices
  if (field.enum_ref) {
    try {
      return enumMembers(field.enum_ref).map((member) => ({ value: member.value, alias: member.alias }))
    } catch {
      return undefined
    }
  }
  if (field.one_of?.length) return field.one_of.map((value) => ({ value, alias: field.name.includes('addr') ? `0x${value.toString(16).toUpperCase().padStart(2, '0')}` : String(value) }))
  return undefined
}

/** A field's start value: its `@default`, else its first choice, else its lowest value, else 0. */
export const fieldDefault = (field: DeviceField): number => {
  if (typeof field.default === 'number') return field.default
  if (field.default && typeof field.default === 'object') return field.default.value
  return fieldChoices(field)?.[0]?.value ?? field.min ?? 0
}

export interface PinUser {
  readonly ownerRef: string
  readonly ownerName: string
  readonly targetDevice: number
  readonly targetPin: number
  readonly use: string
  readonly mode?: number
}

/** The fixed onboard pin connections defined by runit_board_defs.h and runit_board_cfg.c. */
export const BOARD_PIN_LINKS: readonly PinUser[] = [
  // On ESP32 GPIO (id 0):
  { ownerRef: 'board:1', ownerName: 'GPIO expander', targetDevice: 0, targetPin: 8, use: 'Expander RST', mode: 3 },
  { ownerRef: 'board:1', ownerName: 'GPIO expander', targetDevice: 0, targetPin: 9, use: 'Expander INT', mode: 0 },
  { ownerRef: 'board:2', ownerName: 'ADC', targetDevice: 0, targetPin: 42, use: 'ADC ALERT', mode: 0 },

  // On TCA6424A (id 1):
  { ownerRef: 'board:3', ownerName: 'PWM expander', targetDevice: 1, targetPin: 0, use: 'Output enable (OE)', mode: 3 },
  { ownerRef: 'board:10', ownerName: 'Rail A', targetDevice: 1, targetPin: 1, use: 'Regulator INT', mode: 0 },
  { ownerRef: 'board:11', ownerName: 'Rail B', targetDevice: 1, targetPin: 2, use: 'Regulator INT', mode: 0 },
  { ownerRef: 'board:0', ownerName: 'Power system', targetDevice: 1, targetPin: 3, use: 'VUSB OK (eFuse)', mode: 0 },
  { ownerRef: 'board:0', ownerName: 'Power system', targetDevice: 1, targetPin: 4, use: 'VEXT OK (eFuse)', mode: 0 },
  { ownerRef: 'board:12', ownerName: 'Power monitor', targetDevice: 1, targetPin: 5, use: 'INA WARN', mode: 0 },
  { ownerRef: 'board:12', ownerName: 'Power monitor', targetDevice: 1, targetPin: 6, use: 'INA CRIT', mode: 0 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 1, targetPin: 8, use: 'Driver nSLEEP', mode: 3 },
  { ownerRef: 'board:5', ownerName: 'Motor driver 1', targetDevice: 1, targetPin: 9, use: 'Driver nSLEEP', mode: 3 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 1, targetPin: 10, use: 'Driver nFAULT', mode: 0 },
  { ownerRef: 'board:5', ownerName: 'Motor driver 1', targetDevice: 1, targetPin: 11, use: 'Driver nFAULT', mode: 0 },
  { ownerRef: 'board:8', ownerName: 'Motor 2 supply', targetDevice: 1, targetPin: 12, use: 'VM VSUP (mux)', mode: 3 },
  { ownerRef: 'board:8', ownerName: 'Motor 2 supply', targetDevice: 1, targetPin: 13, use: 'VM Rail A (mux)', mode: 3 },
  { ownerRef: 'board:7', ownerName: 'Motor 1 supply', targetDevice: 1, targetPin: 14, use: 'VM VSUP (mux)', mode: 3 },
  { ownerRef: 'board:7', ownerName: 'Motor 1 supply', targetDevice: 1, targetPin: 15, use: 'VM Rail B (mux)', mode: 3 },
  { ownerRef: 'board:11', ownerName: 'Rail B', targetDevice: 1, targetPin: 16, use: 'Regulator EN', mode: 3 },
  { ownerRef: 'board:10', ownerName: 'Rail A', targetDevice: 1, targetPin: 17, use: 'Regulator EN', mode: 3 },
  { ownerRef: 'board:13', ownerName: 'USB-C PD', targetDevice: 1, targetPin: 21, use: 'USB PD INT', mode: 0 },
  { ownerRef: 'board:0', ownerName: 'Board', targetDevice: 1, targetPin: 22, use: 'Spare LED', mode: 3 },
  { ownerRef: 'board:0', ownerName: 'Board', targetDevice: 1, targetPin: 23, use: 'Status LED', mode: 3 },

  // On PCA9685 (id 3):
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 8, use: 'IN1', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 9, use: 'IN2', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 10, use: 'IN3', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 11, use: 'IN4', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 12, use: 'EN1', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 13, use: 'EN2', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 14, use: 'EN3', mode: 6 },
  { ownerRef: 'board:6', ownerName: 'Motor driver 2', targetDevice: 3, targetPin: 15, use: 'EN4', mode: 6 },
]

/** Modes a provider device type supports (from IO_OP_SET_MODE and implied contract ops). */
export const deviceProvidedModes = (type: DeviceType | undefined): Set<number> => {
  if (!type) return new Set()
  const modes = new Set<number>()
  for (const m of devicePinModes(type)) {
    modes.add(m.value)
  }
  for (const contract of type.contracts ?? []) {
    for (const op of contract.ops ?? []) {
      if (op.symbol === 'IO_OP_SET_PWM_DUTY') modes.add(6) // PWM
      if (op.symbol === 'IO_OP_SET_LEVEL') {
        modes.add(3) // Output Push-Pull
        modes.add(4) // Output Open-Drain
        modes.add(5) // Output Open-Drain + Pullup
      }
      if (op.symbol === 'IO_OP_GET_LEVEL') {
        modes.add(0) // Input
        modes.add(1) // Input Pullup
        modes.add(2) // Input Pulldown
      }
      if (op.symbol === 'IO_OP_SET_VOLTAGE') modes.add(8)  // DAC
      if (op.symbol === 'IO_OP_GET_VOLTAGE') modes.add(7)  // ADC
    }
  }
  return modes
}

/** Whether the device provides at least one of the required modes (or true if no mode is required). */
export const deviceProvidesAnyMode = (type: DeviceType | undefined, requiredModes: readonly number[]): boolean => {
  if (!requiredModes.length) return true
  const provided = deviceProvidedModes(type)
  return requiredModes.some((m) => provided.has(m))
}

/** The modes a pin field requires, from its modes array or default_mode. */
export const entryRequiredModes = (field: DeviceField): number[] => {
  if (Array.isArray(field.modes) && field.modes.length > 0) {
    const list: number[] = []
    const extract = (arr: readonly unknown[]) => {
      for (const m of arr) {
        if (typeof m === 'number') list.push(m)
        else if (Array.isArray(m)) extract(m)
        else if (typeof m === 'object' && m !== null && 'value' in m) {
          const val = (m as { value: number }).value
          if (typeof val === 'number') list.push(val)
        }
      }
    }
    extract(field.modes)
    if (list.length > 0) return [...new Set(list)]
  }
  if (field.default_mode !== undefined) return [field.default_mode]
  return []
}

/** The pins a device type provides, optionally filtered by required modes. */
export const getDevicePins = (type: DeviceType | undefined, requiredModes?: readonly number[]): readonly { readonly pin: number; readonly label: string }[] => {
  if (!type) return []
  const pins = new Set<number>()
  let pinAlias = 'Pin'
  for (const c of type.contracts ?? []) {
    for (const op of c.ops ?? []) {
      for (const p of op.params ?? []) {
        if (p.field === 'pin' && Array.isArray(p.one_of)) {
          for (const val of p.one_of) {
            pins.add(typeof val === 'object' && val !== null ? (val as { value: number }).value : Number(val))
          }
          if (p.alias) pinAlias = p.alias
        }
      }
    }
  }
  if (!pins.size) return []
  const allSortedPins = [...pins].sort((a, b) => a - b).map((pin) => ({
    pin,
    label: type.id === 'device_gpio_esp' ? `GPIO ${pin}` : type.id === 'device_pca9685' ? `PWM CH ${pin}` : `${pinAlias} ${pin}`,
  }))

  if (!requiredModes || requiredModes.length === 0) {
    return allSortedPins
  }

  const capablePins = new Set<number>()
  let hasSpecificFilter = false

  for (const mode of requiredModes) {
    let opSymbol: string | undefined
    if (mode === 6) opSymbol = 'IO_OP_SET_PWM_DUTY'
    else if (mode === 7) opSymbol = 'IO_OP_GET_VOLTAGE'
    else if (mode === 8) opSymbol = 'IO_OP_SET_VOLTAGE'
    else if (mode === 0 || mode === 1 || mode === 2) opSymbol = 'IO_OP_GET_LEVEL'
    else if (mode === 3 || mode === 4 || mode === 5) opSymbol = 'IO_OP_SET_LEVEL'

    const op = type.contracts.flatMap((c) => c.ops).find((o) => o.symbol === opSymbol)
    const pinParam = op?.params?.find((p) => p.field === 'pin')
    if (pinParam?.one_of?.length) {
      hasSpecificFilter = true
      for (const val of pinParam.one_of) {
        const pinNum = typeof val === 'object' && val !== null ? (val as { value: number }).value : Number(val)
        capablePins.add(pinNum)
      }
    } else if (deviceProvidedModes(type).has(mode)) {
      for (const p of allSortedPins) {
        capablePins.add(p.pin)
      }
    }
  }

  if (hasSpecificFilter || capablePins.size > 0) {
    return allSortedPins.filter((p) => capablePins.has(p.pin))
  }

  return []
}

/** The IO modes a device supports for IO_OP_SET_MODE. */
export const devicePinModes = (type: DeviceType | undefined): readonly { readonly value: number; readonly alias: string }[] => {
  if (!type) return []
  const setModeOp = type.contracts.flatMap((c) => c.ops).find((op) => op.symbol === 'IO_OP_SET_MODE')
  if (!setModeOp) return []
  const modeParam = setModeOp.params?.find((p) => p.field === 'mode')
  if (modeParam?.one_of?.length) {
    return modeParam.one_of.map((entry) => {
      if (typeof entry === 'object' && entry !== null) {
        const item = entry as { value: number; alias?: string; symbol?: string }
        return { value: item.value, alias: item.alias ?? item.symbol ?? String(item.value) }
      }
      return { value: Number(entry), alias: String(entry) }
    })
  }
  const allModes = [
    { value: 0, alias: 'Input' },
    { value: 1, alias: 'Input with Pullup' },
    { value: 2, alias: 'Input with Pulldown' },
    { value: 3, alias: 'Output (Push-Pull)' },
    { value: 4, alias: 'Output (Open-Drain)' },
    { value: 5, alias: 'Output (Open-Drain + Pullup)' },
  ]
  if (type.id === 'device_gpio_esp') {
    return [
      ...allModes,
      { value: 6, alias: 'PWM' },
      { value: 7, alias: 'ADC' },
    ]
  }
  return allModes
}

export const isOutputMode = (mode: number | undefined): boolean => mode === 3 || mode === 4 || mode === 5

/** Format a pin label with user alias if present. */
export const pinDisplayLabel = (
  pinAliases: Readonly<Record<string, Readonly<Record<string, string>>>> | undefined,
  deviceRef: string,
  pin: number,
  baseLabel?: string
): string => {
  const alias = pinAliases?.[deviceRef]?.[String(pin)]
  if (alias) return `${alias} (pin ${pin})`
  return baseLabel && baseLabel !== String(pin) ? `${baseLabel} (${pin})` : `Pin ${pin}`
}

