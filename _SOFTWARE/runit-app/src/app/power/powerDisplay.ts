import type { BoardDescriptor, CommandDef, FieldValues, ServiceCatalog, WireField } from '@backend'
import { answers } from '../devices/devices'
import type { DeviceInfo, DeviceOps } from '../devices/devices'

/*
 * The Live power display, pure: what it reads (the project's `powerDisplay`, else the board's default), the commands a reading can
 * use, a reading's unit and how a value is shown. A reading is a name and the read of a command that answers a value: a service's
 * (`POWER_MANAGER STATUS`) or a device's (`POWER_MONITOR VOLTAGE` on a channel), and the field of a struct answer.
 */

/** What a reading sends (the command's read): a command of a service, or of a device (one of its contracts', or its type's own). */
export type PowerRead =
  | { readonly kind: 'service'; readonly service: string; readonly command: string }
  | { readonly kind: 'device'; readonly device: number; readonly contract: string; readonly command: string }

export interface PowerReading {
  readonly name: string
  readonly read: PowerRead
  readonly args?: FieldValues
  /** The field of a struct answer; none for an answer of one value. */
  readonly field?: string
  /** Shown in the short view; the others only with More. */
  readonly main?: boolean
}

const STATUS = { kind: 'service', service: 'POWER_MANAGER', command: 'STATUS' } as const
const monitor = (device: number, command: string) => ({ kind: 'device', device, contract: 'POWER_MONITOR', command }) as const

/** The board's own display: the input's source, voltage and power in short, each regulator rail's voltage and current with More (board.json's `power`). */
export const defaultReadings = (board: BoardDescriptor): PowerReading[] => {
  const input = board.power.input
  return [
    { name: 'Input', read: STATUS, field: 'source', main: true },
    { name: 'Input', read: STATUS, field: 'input_mV', main: true },
    ...(input ? [{ name: 'Input', read: monitor(input.device, 'POWER'), args: { channel: input.channel }, main: true }] : []),
    ...board.power.rails.flatMap((rail): PowerReading[] => rail.monitor
      ? [
        { name: rail.name, read: monitor(rail.monitor.device, 'VOLTAGE'), args: { channel: rail.monitor.channel } },
        { name: rail.name, read: monitor(rail.monitor.device, 'CURRENT'), args: { channel: rail.monitor.channel } },
      ]
      : []),
  ]
}

/** The command a reading sends, from the catalog; undefined when the catalog has no such command. */
export const commandOf = (catalog: ServiceCatalog, devices: readonly DeviceInfo[], read: PowerRead): CommandDef | undefined => {
  if (read.kind === 'service') return catalog.services.get(read.service)?.commands.get(read.command)
  const device = devices.find((d) => d.id === read.device)
  const owner = catalog.contracts.get(read.contract) ?? (device?.type?.id === read.contract ? device.type : undefined)
  return owner?.commands.get(read.command)
}

/** A command whose read answers something: a display can send it every second. */
const readable = (command: CommandDef): boolean => command.read?.returns !== undefined

/** The descriptors' category of what is about power (`"category": "power"`: the power manager, the power contracts). */
const POWER = 'power'

/**
 * Every command a reading can use, all about power: the power services' that read (`category` power: the power manager), and on
 * each device that has a power contract, its power contracts' and its own commands that read.
 */
/**
 * Every reading a display can take: the power services' reads, then each device's power contracts and own reads. With a device's OPS
 * answer (`opsOf`, read while connected) only what it answers (the DRV8962 reads a current, and only with its IPROPI pins wired).
 */
export const readCommands = (catalog: ServiceCatalog, devices: readonly DeviceInfo[], opsOf?: (device: number) => DeviceOps | undefined): { readonly read: PowerRead; readonly label: string; readonly group: string }[] => [
  ...[...catalog.services.values()].filter((service) => service.category === POWER).flatMap((service) => [...service.commands.values()].filter(readable)
    .map((c) => ({ read: { kind: 'service' as const, service: service.id, command: c.id }, label: c.label, group: service.label }))),
  ...devices.flatMap((device) => {
    const contracts = (device.type?.contracts ?? []).flatMap((id) => {
      const contract = catalog.contracts.get(id)
      return contract?.category === POWER ? [contract] : []
    })
    if (contracts.length === 0) return []
    const ops = opsOf?.(device.id)
    return [
      ...contracts.flatMap((contract) => [...contract.commands.values()].filter((c) => readable(c) && answers(catalog, ops, contract, c, 'read')).map((c) => ({ read: { kind: 'device' as const, device: device.id, contract: contract.id, command: c.id }, label: `${contract.label}: ${c.label}`, group: device.name }))),
      ...(device.type ? [...device.type.commands.values()].filter((c) => readable(c) && answers(catalog, ops, device.type!, c, 'read')).map((c) => ({ read: { kind: 'device' as const, device: device.id, contract: device.type!.id, command: c.id }, label: c.label, group: device.name })) : []),
    ]
  }),
]

export const readKey = (read: PowerRead): string => (read.kind === 'service' ? `service:${read.service}:${read.command}` : `device:${read.device}:${read.contract}:${read.command}`)

/** The arguments of a reading: the key its read takes (a channel). */
export const readArgs = (command: CommandDef | undefined): readonly WireField[] => command?.read?.args?.fields ?? []

/** The fields of a struct answer of the read; none for a one-value answer. */
export const answerFields = (command: CommandDef | undefined): readonly WireField[] => {
  const returns = command?.read?.returns
  return returns?.kind === 'struct' ? returns.layout.fields : []
}

/** What a reading's value is: its field (struct answer) or the answer's own unit, label and choices. */
export const valueSpec = (command: CommandDef | undefined, field: string | undefined): { readonly unit?: string; readonly label?: string; readonly field?: WireField } => {
  const returns = command?.read?.returns
  if (!returns) return {}
  if (returns.kind === 'struct') {
    const f = returns.layout.fields.find((x) => x.id === field)
    return f ? { ...(f.unit ? { unit: f.unit } : {}), label: f.label, field: f } : {}
  }
  return { ...(returns.unit ? { unit: returns.unit } : {}), ...(returns.label ? { label: returns.label } : {}) }
}

/** A reading in its unit, short: `12.0V`, `0.85A`, `10.2W`. */
export const formatReading = (value: number, unit: string | undefined): string => {
  if (unit === 'mV') return `${(value / 1000).toFixed(Math.abs(value) < 10000 ? 2 : 1)}V`
  if (unit === 'mA') return `${(value / 1000).toFixed(2)}A`
  if (unit === 'mW') return `${(value / 1000).toFixed(1)}W`
  return unit ? `${value}${unit}` : String(value)
}

/** A value as the display shows it: a number in its unit, a choice by its text (its choices, else the `0 unknown, 1 USB` its description lists). */
export const showValue = (value: unknown, spec: ReturnType<typeof valueSpec>): string => {
  if (value === undefined) return '–'
  const n = typeof value === 'number' ? value : typeof value === 'bigint' ? Number(value) : undefined
  const field = spec.field
  if (field?.type === 'choice' || field?.choices) {
    const found = field.choices?.find((c) => c.value === value)
    if (found) return found.label
    const listed = [...(field.description ?? '').matchAll(/(\d+)\s+([^,]+)/g)].find((m) => Number(m[1]) === n)
    if (listed) return listed[2]!.trim()
  }
  if (n === undefined) return String(value)
  if (spec.unit === '%' && n === 0xff) return '?'
  return formatReading(n, spec.unit)
}
