import { commandFromDefinition, DescriptorError, parseByte } from './commandCatalog'
import type { CommandCatalog, CommandDescriptor, CommandFieldInfo, CommandGroup } from './commandCatalog'
import type { GeneratedBoardFile, GeneratedChoice, GeneratedEnumsFile, GeneratedLayout } from './generatedTypes'

/*
 * Devices the firmware can run (data-structures/devices/*.generated.json, one
 * per annotated dec_device_*.h) and the ones the board installs itself
 * (board.generated.json). A device type knows its install packet (class 0x01,
 * 0x40–0x4F), its pin groups (device + pin + mode, "none" by a sentinel) and
 * the contracts it answers, with the device ID parameter marked `instance`
 * (filled from the device, never typed).
 */

/** `data-structures/devices/*.generated.json` (the parts the app reads) */
export interface GeneratedDeviceFile {
  readonly id: string
  readonly title: string
  readonly description: string
  readonly protocols?: readonly string[]
  readonly tags?: readonly string[]
  readonly datasheet?: string
  readonly pwm_frequencies?: number
  readonly contractProvider?: { readonly symbol: string; readonly value: number; readonly alias?: string; readonly description?: string }
  readonly install: {
    readonly packet: string
    readonly packet_definition: GeneratedLayout & {
      readonly class_header: string
      readonly packet_header: string
      /** Keyed by the sys_io_pin_ref_t field (`intr_pin`), the name board.generated.json uses for the pins its devices take. */
      readonly groups?: Readonly<Record<string, { readonly fields: readonly string[]; readonly sentinel_field?: string; readonly alias?: string; readonly note?: string } | undefined>>
    }
  }
  readonly contracts: readonly {
    readonly packet: string
    readonly alias?: string
    readonly description?: string
    readonly returns?: string
    readonly parameters: readonly GeneratedDeviceParameter[]
  }[]
}

export interface GeneratedDeviceParameter {
  readonly name: string
  readonly alias?: string
  readonly instance?: boolean
  readonly device_wide?: boolean
  readonly one_of?: readonly (number | GeneratedChoice)[]
  readonly enum_ref?: string
  readonly type?: string
  readonly unit?: string
  readonly min?: number | string
  readonly max?: number | string
  readonly default?: number | string
  readonly note?: string
}

export interface DeviceChoice {
  readonly value: number
  readonly label: string
  /** Enum member name, when the choice is one (e.g. `SYS_IO_MODE_INPUT`). */
  readonly symbol?: string
  readonly description?: string
}

export interface DeviceParameter {
  readonly name: string
  readonly label: string
  /** The device ID: filled from the device the contract runs on. */
  readonly instance: boolean
  /** Selects nothing on this device (the setting is the whole device's): hidden, sent as 0. */
  readonly deviceWide: boolean
  readonly field: CommandFieldInfo
  readonly choices?: readonly DeviceChoice[]
  /** A 0/1 value shown as a switch. */
  readonly boolean: boolean
  readonly unit?: string
  readonly min?: number
  readonly max?: number
  readonly defaultValue?: number
  /** The header's @note (the parameter's, its property's, else the generic packet field's). */
  readonly note?: string
}

export interface DeviceContract {
  /** Packet struct name, e.g. `packet_sys_io_set_level_t`. */
  readonly id: string
  readonly label: string
  readonly description?: string
  /** `device`: lifecycle commands every device takes (reset, suspend …); `contract`: what the device type answers. */
  readonly kind: 'device' | 'contract'
  readonly command: CommandDescriptor
  readonly parameters: readonly DeviceParameter[]
  /** Name of the value an OK answer carries (`voltage_mV`). */
  readonly returns?: string
}

/** Fields that describe one pin on another device: device ID + pin (+ mode); the sentinel field set to its sentinel means "none". */
export interface InstallPinGroup {
  /** The sys_io_pin_ref_t field, e.g. `intr_pin`. */
  readonly key: string
  /** What the pin is for: the header's @alias, else a name from the field (`Interrupt`). */
  readonly use: string
  /** `<use> pin` */
  readonly label: string
  readonly note?: string
  readonly deviceField?: string
  readonly pinField?: string
  readonly modeField?: string
  readonly sentinelField?: string
  readonly sentinel: number
}

export interface DeviceType {
  /** Descriptor ID, e.g. `device_pca9685`. */
  readonly id: string
  readonly title: string
  readonly description: string
  readonly tags: readonly string[]
  readonly protocols: readonly string[]
  readonly datasheet?: string
  /** Different PWM frequencies the device runs at once (per-pin frequencies on shared timers). */
  readonly pwmFrequencies?: number
  readonly provider?: { readonly value: number; readonly label: string; readonly description?: string }
  /** The install packet; its `device_id` field is filled from the device. */
  readonly install: CommandDescriptor
  readonly installChoices: ReadonlyMap<string, readonly DeviceChoice[]>
  readonly pinGroups: readonly InstallPinGroup[]
  readonly contracts: readonly DeviceContract[]
}

/** A pin one device takes on another (install pin reference). */
export interface BoardPinLink {
  /** Config field, e.g. `intr_pin`, `in_pins[2]`. */
  readonly use: string
  /** What the pin is for, e.g. `Interrupt` (the device type's pin group alias when it has one). */
  readonly label: string
  readonly deviceId: number
  readonly pin: number
  /** sys_io_mode_e value. */
  readonly mode: number
}

export interface BoardDevice {
  readonly deviceId: number
  readonly symbol: string
  readonly name: string
  readonly title: string
  readonly type?: DeviceType
  /** The board installs it at boot (bring-up switch on in runit_board_cfg.c). */
  readonly installed: boolean
  readonly i2c?: { readonly bus: number; readonly address: number }
  readonly pins: readonly BoardPinLink[]
}

/** A pin the board sets up itself at boot. */
export interface BoardPinSetup {
  readonly deviceId: number
  readonly pin: number
  readonly mode: number
  readonly level?: boolean
  readonly label?: string
}

export interface DeviceCatalog {
  /** App-only display overrides, keyed by project device reference. */
  readonly deviceAliases?: Readonly<Record<string, string>>
  readonly pinAliases?: Readonly<Record<string, Readonly<Record<string, string>>>>
  readonly types: readonly DeviceType[]
  type(id: string): DeviceType | undefined
  /** The board's own devices (installed at boot by boot action 1 unless a bring-up switch is off). */
  readonly board: readonly BoardDevice[]
  /** Pins the board sets up at boot (only on installed devices). */
  readonly boardPinSetup: readonly BoardPinSetup[]
  /** I2C buses: onboard devices on `internal`, user devices on `user`. */
  readonly i2cBuses: { readonly internal: number; readonly user: number }
  /** sys_io_mode_e, for naming pin modes. */
  readonly ioModes: readonly DeviceChoice[]
  /** The built-in static actions (sys_action_static_e): freeze, suspend, reset … */
  readonly staticActions: readonly DeviceChoice[]
  /** sys_device_importance_e: which error severities reach device policy. */
  readonly importance: readonly DeviceChoice[]
  /** ESP pins the board uses outside sys_io (I2C buses …): never free. */
  readonly reservedPins: readonly { readonly deviceId: number; readonly pin: number; readonly label: string }[]
  /** Lifecycle commands every installed device takes. */
  readonly lifecycle: readonly DeviceContract[]
  /** Highest device ID the firmware accepts (install packets' `device_id` max). */
  readonly maxDeviceId: number
  /** The device type an install frame (class, packet) belongs to. */
  typeByInstall(classHeader: number, packetHeader: number): DeviceType | undefined
}

const LIFECYCLE = ['uninstall', 'reset', 'suspend', 'resume', 'freeze', 'sync', 'set_error_handling']

const toNumber = (value: number | string | undefined): number | undefined => {
  if (typeof value === 'number') return value
  if (value === undefined) return undefined
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : undefined
}

const enumChoices = (enums: GeneratedEnumsFile, name: string | undefined): DeviceChoice[] | undefined =>
  name ? enums.enums[name]?.members.map((member) => ({ value: member.value, label: member.alias ?? member.name, symbol: member.name, ...(member.description ? { description: member.description } : {}) })) : undefined

const choicesOf = (entries: readonly (number | GeneratedChoice)[] | undefined): DeviceChoice[] | undefined =>
  entries?.map((entry) => (typeof entry === 'number' ? { value: entry, label: String(entry) } : { value: entry.value, label: entry.alias ?? entry.symbol, symbol: entry.symbol, ...(entry.description ? { description: entry.description } : {}) }))

/** A sys_io_mode_e symbol from board.generated.json as its value. */
const ioMode = (enums: GeneratedEnumsFile, symbol: string): number => {
  const value = enums.enums.sys_io_mode_e?.members.find((member) => member.name === symbol)?.value
  if (value === undefined) throw new DescriptorError(`board.generated.json names IO mode ${symbol}, which enums.json lacks.`)
  return value
}

const parameterFrom = (generated: GeneratedDeviceParameter | undefined, field: CommandFieldInfo, enums: GeneratedEnumsFile): DeviceParameter => ({
  name: field.name,
  label: generated?.alias ?? field.label,
  instance: generated?.instance === true || (!generated && field.name === 'device_id'),
  deviceWide: generated?.device_wide === true,
  field,
  choices: choicesOf(generated?.one_of) ?? choicesOf(field.choices) ?? enumChoices(enums, generated?.enum_ref ?? field.enumRef),
  boolean: generated?.type === 'bool' || field.type === 'bool',
  unit: generated?.unit ?? field.unit,
  min: toNumber(generated?.min) ?? field.min,
  max: toNumber(generated?.max) ?? field.max,
  defaultValue: toNumber(generated?.default) ?? field.defaultValue,
  ...((generated?.note ?? field.note) ? { note: generated?.note ?? field.note } : {}),
})

const contractFrom = (command: CommandDescriptor, kind: DeviceContract['kind'], enums: GeneratedEnumsFile, generated?: GeneratedDeviceFile['contracts'][number]): DeviceContract => {
  const byName = new Map((generated?.parameters ?? []).map((parameter) => [parameter.name, parameter]))
  return {
    id: command.id,
    label: generated?.alias ?? command.name.replace(/^sys_(device_)?/, '').replaceAll('_', ' '),
    ...(generated?.description ? { description: generated.description } : {}),
    kind,
    command,
    parameters: command.request.fields.map((field) => parameterFrom(byName.get(field.name), field, enums)),
    ...(generated?.returns ? { returns: generated.returns } : {}),
  }
}

/** Readable names of the pins device configs take (`<name>_pin`, `<name>_pins[i]`). */
const PIN_NAMES: Readonly<Record<string, string>> = {
  intr: 'Interrupt',
  rst: 'Reset',
  en: 'Enable',
  oe: 'Output enable',
  crit: 'Critical alert',
  warn: 'Warning alert',
  in: 'Input',
  nsleep: 'nSLEEP',
  nfault: 'nFAULT',
  vref_dac: 'VREF (DAC)',
  current_adc: 'Current sense (ADC)',
}

/** `intr_pin` → `Interrupt`, `in_pins[2]` → `Input 3`, `oe_pin_device_id` → `Output enable`. */
export const pinUseLabel = (use: string): string => {
  const match = /^(?<name>.*?)_pins?(?:_\w+)?(?:\[(?<index>\d+)\])?$/.exec(use)
  const name = match?.groups?.name ?? use
  const label = PIN_NAMES[name] ?? name.replaceAll('_', ' ').replace(/^./, (c) => c.toUpperCase())
  return match?.groups?.index === undefined ? label : `${label} ${Number(match.groups.index) + 1}`
}

const pinGroupsOf = (definition: GeneratedDeviceFile['install']['packet_definition']): InstallPinGroup[] =>
  Object.entries(definition.groups ?? {}).flatMap(([key, group]) => {
    if (!group) return []
    const role = (suffix: string) => group.fields.find((name) => name.endsWith(`_${suffix}`))
    const sentinelField = group.sentinel_field
    const sentinel = sentinelField ? (definition.fields[sentinelField]?.sentinel ?? 255) : 255
    const use = group.alias ?? pinUseLabel(group.fields[0] ?? key)
    return [{
      key,
      use,
      label: `${use} pin`,
      ...(group.note ? { note: group.note } : {}),
      deviceField: role('device_id'),
      pinField: role('pin'),
      modeField: role('mode'),
      sentinelField,
      sentinel: sentinel ?? 255,
    }]
  })

export const buildDeviceCatalog = (files: readonly GeneratedDeviceFile[], board: GeneratedBoardFile, commands: CommandCatalog, enums: GeneratedEnumsFile): DeviceCatalog => {
  const system = commands.groups.find((group) => group.id === 'system')
  if (!system) throw new DescriptorError('The contracts catalog has no system group (class 0x01).')
  const installGroup: CommandGroup = { ...system, title: 'Device install' }

  const types = files.map((file): DeviceType => {
    const definition = file.install.packet_definition
    const install = commandFromDefinition(file.install.packet, parseByte(definition.class_header, file.id), parseByte(definition.packet_header, file.id), definition, installGroup)
    const installChoices = new Map<string, readonly DeviceChoice[]>()
    for (const field of install.request.fields) {
      const choices = choicesOf(field.choices) ?? enumChoices(enums, field.enumRef)
      if (choices) installChoices.set(field.name, choices)
    }
    const contracts = file.contracts.map((contract) => {
      const command = commands.get(contract.packet)
      if (!command) throw new DescriptorError(`${file.id}: contract ${contract.packet} is not in the contracts catalog.`)
      return contractFrom(command, 'contract', enums, contract)
    })
    return {
      id: file.id,
      title: file.title,
      description: file.description,
      tags: file.tags ?? [],
      protocols: file.protocols ?? [],
      ...(file.datasheet ? { datasheet: file.datasheet } : {}),
      ...(file.pwm_frequencies ? { pwmFrequencies: file.pwm_frequencies } : {}),
      ...(file.contractProvider ? { provider: { value: file.contractProvider.value, label: file.contractProvider.alias ?? file.contractProvider.symbol, ...(file.contractProvider.description ? { description: file.contractProvider.description } : {}) } } : {}),
      install,
      installChoices,
      pinGroups: pinGroupsOf(definition),
      contracts,
    }
  }).sort((a, b) => a.title.localeCompare(b.title))

  const byId = new Map(types.map((type) => [type.id, type]))
  const byInstall = new Map(types.map((type) => [(type.install.classHeader << 8) | type.install.packetHeader, type]))
  const lifecycle = LIFECYCLE.flatMap((name) => {
    const command = commands.get(`packet_sys_device_${name}_t`)
    return command ? [contractFrom(command, 'device', enums)] : []
  })
  const maxDeviceId = Math.min(...types.map((type) => type.install.request.fields.find((field) => field.name === 'device_id')?.max ?? 127))

  const installed = new Set(board.devices.filter((device) => device.installed).map((device) => device.id))
  return {
    types,
    type: (id) => byId.get(id),
    board: board.devices.map((device) => {
      const type = device.descriptor ? byId.get(device.descriptor) : undefined
      return {
        deviceId: device.id,
        symbol: device.symbol,
        name: device.name,
        title: device.title ?? type?.title ?? device.name,
        ...(type ? { type } : {}),
        installed: device.installed,
        ...(device.i2c ? { i2c: device.i2c } : {}),
        pins: (device.pins ?? []).map((pin) => ({ use: pin.use, label: type?.pinGroups.find((group) => group.key === pin.use)?.use ?? pinUseLabel(pin.use), deviceId: pin.device, pin: pin.pin, mode: ioMode(enums, pin.mode) })),
      }
    }),
    boardPinSetup: board.pin_setup.filter((entry) => entry.installed && installed.has(entry.device)).map((entry) => ({
      deviceId: entry.device,
      pin: entry.pin,
      mode: ioMode(enums, entry.mode),
      ...(entry.level === undefined ? {} : { level: entry.level }),
      ...(entry.label ? { label: entry.label } : {}),
    })),
    i2cBuses: board.i2c_buses,
    ioModes: enumChoices(enums, 'sys_io_mode_e') ?? [],
    staticActions: enumChoices(enums, 'sys_action_static_e') ?? [],
    importance: enumChoices(enums, 'sys_device_importance_e') ?? [],
    reservedPins: board.reserved_pins.map((entry) => ({ deviceId: entry.device, pin: entry.pin, label: entry.label })),
    lifecycle,
    maxDeviceId,
    typeByInstall: (classHeader, packetHeader) => byInstall.get((classHeader << 8) | packetHeader),
  }
}
