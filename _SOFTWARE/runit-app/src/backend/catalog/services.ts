import type { EnumTables } from '@engine/generated/wire'
import { RAW_TYPES, rawWidth } from './codec'
import type { Choice, Layout, RawType, WireField } from './codec'

/*
 * Every command the board takes, read from the descriptors the firmware publishes (`templates/contract.schema.json`,
 * `device.schema.json`): the system services and the device contracts (`generated/services`, `generated/contracts`) and the
 * device types (`generated/devices`). No service, contract, command or field is named in this code; a new descriptor is a new
 * entry with nothing to edit here. The descriptor's kind decides only how it is addressed:
 *
 *   service    a descriptor with a `router_key`: its commands are `[router key][op][args]`
 *   contract   a descriptor with a `contract_id` and no router key: ops a device answers, `(contract id << 8) | op` (SYS_DEVICE.MD)
 *   device     a device type (`type_id`): the contracts it implements, its own ops (contract `SYS_DEVICE_CONTRACT_OWN`) and the
 *              config struct its CREATE carries
 *
 * A descriptor that does not hold together (fields that overlap, a router key or a contract id the firmware does not publish,
 * an op twice) is reported in `problems`, not thrown: the rest of the catalog stays usable.
 */

export type EntryKind = 'service' | 'contract' | 'device'

export type Returns =
  | { readonly kind: 'struct'; readonly layout: Layout; readonly array: boolean; readonly label?: string; readonly description?: string }
  | { readonly kind: 'value'; readonly rawType: RawType; readonly boolean: boolean; readonly array: boolean; readonly unit?: string; readonly label?: string; readonly description?: string; readonly choices?: readonly Choice[] }

export interface CommandDef {
  readonly id: string
  readonly op: number
  readonly label: string
  readonly description?: string
  readonly urgent: boolean
  readonly pinScoped: boolean
  readonly args?: Layout
  readonly returns?: Returns
}

export interface EventDef {
  readonly id: string
  readonly code?: number
  readonly label: string
  readonly description?: string
  readonly payload?: Layout
}

export interface CatalogEntry {
  readonly kind: EntryKind
  readonly id: string
  readonly label: string
  readonly description?: string
  /** The descriptor's own `category` (`system`, `power`, `gpio`, ...): for grouping in a UI. */
  readonly category?: string
  readonly tags: readonly string[]
  /** The router key of a service. */
  readonly key?: number
  /** The contract id of a contract (a service may have one too: the id its events are published under). */
  readonly contractId?: number
  /** A device type: its type id, the contracts it implements and its config. */
  readonly typeId?: number
  readonly contracts: readonly string[]
  readonly config?: Layout
  readonly commands: ReadonlyMap<string, CommandDef>
  readonly events: readonly EventDef[]
  /** The descriptor's file, for messages. */
  readonly source: string
}

/** A descriptor as JSON, with the name of the file it came from. */
export interface DescriptorFile {
  readonly source: string
  readonly json: unknown
}

type Json = Record<string, unknown>
const obj = (v: unknown): Json | undefined => (v && typeof v === 'object' && !Array.isArray(v) ? (v as Json) : undefined)
const arr = (v: unknown): readonly unknown[] => (Array.isArray(v) ? v : [])
const str = (v: unknown): string | undefined => (typeof v === 'string' ? v : undefined)
const num = (v: unknown): number | undefined => (typeof v === 'number' && Number.isFinite(v) ? v : undefined)

const isRaw = (v: unknown): v is RawType => typeof v === 'string' && (RAW_TYPES as readonly string[]).includes(v)

export class ServiceCatalog {
  /** Services by id, and by router key. */
  readonly services = new Map<string, CatalogEntry>()
  readonly byKey = new Map<number, CatalogEntry>()
  /** Device contracts by id, and by contract id. */
  readonly contracts = new Map<string, CatalogEntry>()
  readonly byContractId = new Map<number, CatalogEntry>()
  /** Device types by id, and by type id. */
  readonly devices = new Map<string, CatalogEntry>()
  readonly byTypeId = new Map<number, CatalogEntry>()
  readonly problems: string[] = []
  /** The contract id of a device's own ops (`device_contract`, `SYS_DEVICE_CONTRACT_OWN`). */
  readonly ownContractId: number | undefined
  /** The contract id of the lifecycle ops every device answers (`SYS_DEVICE_CONTRACT_DEVICE`: CREATE, INFO, OPS, ...). */
  readonly lifecycleContractId: number | undefined

  constructor(
    files: readonly DescriptorFile[],
    private readonly enums: EnumTables,
  ) {
    const own = enums.device_contract?.members.find((m) => m.name === 'SYS_DEVICE_CONTRACT_OWN')
    this.ownContractId = own?.value
    if (own === undefined) this.problems.push("generated/common/enums.json: group 'device_contract' has no SYS_DEVICE_CONTRACT_OWN: no device's own ops can be addressed.")
    this.lifecycleContractId = enums.device_contract?.members.find((m) => m.name === 'SYS_DEVICE_CONTRACT_DEVICE')?.value
    for (const file of files) this.add(file)
    for (const device of this.devices.values()) {
      for (const contract of device.contracts) if (!this.contracts.has(contract)) this.problems.push(`${device.source}: contract ${contract} is not described.`)
    }
  }

  /** Every entry of every kind. */
  get entries(): CatalogEntry[] {
    return [...this.services.values(), ...this.contracts.values(), ...this.devices.values()]
  }

  service(id: string): CatalogEntry {
    const entry = this.services.get(id)
    if (!entry) throw new Error(`No service ${id} (${[...this.services.keys()].join(', ')}).`)
    return entry
  }

  contract(id: string): CatalogEntry {
    const entry = this.contracts.get(id)
    if (!entry) throw new Error(`No device contract ${id} (${[...this.contracts.keys()].join(', ')}).`)
    return entry
  }

  device(id: string | number): CatalogEntry {
    const entry = typeof id === 'number' ? this.byTypeId.get(id) : this.devices.get(id)
    if (!entry) throw new Error(`No device type ${id} (${[...this.devices.keys()].join(', ')}).`)
    return entry
  }

  command(entry: CatalogEntry, id: string): CommandDef {
    const command = entry.commands.get(id)
    if (!command) throw new Error(`${entry.id} has no command ${id} (${[...entry.commands.keys()].join(', ')}).`)
    return command
  }

  private add(file: DescriptorFile): void {
    const json = obj(file.json)
    const id = str(json?.id)
    if (!json || !id) return void this.problems.push(`${file.source}: not a descriptor (no id).`)
    const problem = (text: string): void => void this.problems.push(`${file.source}: ${text}`)
    const key = num(json.router_key)
    const contractId = num(json.contract_id)
    const typeId = num(json.type_id)
    const kind: EntryKind | undefined = typeId !== undefined ? 'device' : key !== undefined ? 'service' : contractId !== undefined ? 'contract' : undefined
    if (!kind) return problem('neither a router key, a contract id nor a type id: nothing addresses it.')

    const commands = new Map<string, CommandDef>()
    const ops = new Map<number, string>()
    for (const raw of arr(json.commands)) {
      const command = this.parseCommand(raw, problem)
      if (!command) continue
      if (commands.has(command.id)) problem(`command ${command.id} twice.`)
      const other = ops.get(command.op)
      if (other !== undefined) problem(`op ${command.op} is both ${other} and ${command.id}.`)
      commands.set(command.id, command)
      ops.set(command.op, command.id)
    }
    const configJson = obj(json.config) ?? (Array.isArray(json.config) ? { fields: json.config } : undefined)
    const entry: CatalogEntry = {
      kind,
      id,
      label: str(json.alias) ?? str(json.label) ?? id,
      ...(str(json.description) ? { description: str(json.description) } : {}),
      ...(str(json.category) ? { category: str(json.category) } : {}),
      tags: arr(json.tags).filter((t): t is string => typeof t === 'string'),
      ...(key !== undefined ? { key } : {}),
      ...(contractId !== undefined ? { contractId } : {}),
      ...(typeId !== undefined ? { typeId } : {}),
      contracts: arr(json.contracts).filter((c): c is string => typeof c === 'string'),
      ...(kind === 'device' && configJson ? { config: this.layout(configJson, `config`, problem, num(configJson.size)) } : {}),
      commands,
      events: arr(json.events).flatMap((e) => this.event(e, problem)),
      source: file.source,
    }

    if (key !== undefined && !this.enumHas('router_key', key)) problem(`router key ${key} is not one the firmware publishes (router_key).`)
    if (contractId !== undefined && !this.enumHas('device_contract', contractId)) problem(`contract id ${contractId} is not one the firmware publishes (device_contract).`)

    const [map, index, number] =
      kind === 'service' ? [this.services, this.byKey, key!] : kind === 'contract' ? [this.contracts, this.byContractId, contractId!] : [this.devices, this.byTypeId, typeId!]
    const taken = map.get(id) ?? index.get(number)
    if (taken) return problem(`${kind} ${id} (${number}) is already ${taken.source}.`)
    map.set(id, entry)
    index.set(number, entry)
  }

  private enumHas(group: string, value: number): boolean {
    return this.enums[group]?.members.some((m) => m.value === value) ?? false
  }

  private parseCommand(raw: unknown, problem: (text: string) => void): CommandDef | undefined {
    const json = obj(raw)
    const id = str(json?.id)
    const op = num(json?.op)
    if (!json || !id) return void problem('a command without an id.')
    if (op === undefined || !Number.isInteger(op) || op < 0 || op > 0xff) return void problem(`command ${id}: op ${String(json.op)} is not a byte.`)
    const where = `command ${id}`
    const argsJson = Array.isArray(json.args) ? { fields: json.args } : obj(json.args)
    const args = argsJson ? this.layout(argsJson, `${where} args`, problem) : undefined
    const returns = obj(json.returns) ? this.returns(obj(json.returns)!, `${where} returns`, problem) : undefined
    return {
      id,
      op,
      label: str(json.label) ?? str(json.alias) ?? id,
      ...(str(json.description) ? { description: str(json.description) } : {}),
      urgent: json.urgent === true,
      pinScoped: json.pin_scoped === true,
      ...(args ? { args } : {}),
      ...(returns ? { returns } : {}),
    }
  }

  private returns(json: Json, where: string, problem: (text: string) => void): Returns | undefined {
    const common = {
      array: json.array === true,
      ...(str(json.label) ?? str(json.alias) ? { label: str(json.label) ?? str(json.alias) } : {}),
      ...(str(json.description) ? { description: str(json.description) } : {}),
    }
    if (Array.isArray(json.fields)) return { kind: 'struct', layout: this.layout(json, where, problem, num(json.size)), ...common }
    const type = str(json.type)
    const boolean = type === 'boolean'
    const rawType = boolean ? 'u8' : type
    if (!isRaw(rawType)) return void problem(`${where}: type ${String(json.type)} has no wire layout.`)
    const choices = this.choices(json.choices)
    return { kind: 'value', rawType, boolean, ...common, ...(str(json.unit) ? { unit: str(json.unit) } : {}), ...(choices ? { choices } : {}) }
  }

  private event(raw: unknown, problem: (text: string) => void): EventDef[] {
    const json = obj(raw)
    const id = str(json?.id)
    if (!json || !id) {
      problem('an event without an id.')
      return []
    }
    const payload = Array.isArray(json.payload) && json.payload.length > 0 ? this.layout({ fields: json.payload }, `event ${id}`, problem) : undefined
    return [{ id, ...(num(json.code) !== undefined ? { code: num(json.code) } : {}), label: str(json.label) ?? str(json.alias) ?? id, ...(str(json.description) ? { description: str(json.description) } : {}), ...(payload ? { payload } : {}) }]
  }

  private choices(raw: unknown, group?: string): Choice[] | undefined {
    if (group) {
      const table = this.enums[group]
      return table?.members.map((m) => ({ label: m.alias ?? m.name, value: m.value, name: m.name, ...(m.description ? { description: m.description } : {}) }))
    }
    const list = arr(raw).flatMap((c) => {
      const json = obj(c)
      const value = json?.value
      if (!json || typeof json.label !== 'string' || !['number', 'string', 'boolean'].includes(typeof value)) return []
      return [{ label: json.label, value: value as Choice['value'], ...(str(json.description) ? { description: str(json.description) } : {}) }]
    })
    return list.length > 0 ? list : undefined
  }

  /**
   * The fields of a struct, placed: a field without an offset follows the one before, one without a size takes its raw type's
   * width, and a `bytes` / `string` field without a size runs to the end (it must be the last).
   */
  private layout(json: Json, where: string, problem: (text: string) => void, declared?: number): Layout {
    const fields: WireField[] = []
    let cursor = 0
    let fixed = 0
    let variable = false
    for (const raw of arr(json.fields)) {
      const f = obj(raw)
      const id = str(f?.id)
      if (!f || !id) {
        problem(`${where}: a field without an id.`)
        continue
      }
      const rawType = f.raw_type ?? (f.type === 'boolean' ? 'u8' : undefined)
      if (!isRaw(rawType)) {
        problem(`${where}: field ${id} has no raw_type the wire knows (${String(f.raw_type)}).`)
        continue
      }
      if (variable) problem(`${where}: field ${id} follows a field that runs to the end.`)
      const offset = num(f.offset) ?? cursor
      const width = rawWidth(rawType)
      const size = num(f.size) ?? width
      if (width !== undefined && (size === undefined || size < width || size % width !== 0)) problem(`${where}: field ${id} is ${rawType} but ${size} bytes.`)
      const overlap = size === undefined ? undefined : fields.find((o) => o.size !== undefined && offset < o.offset + o.size && o.offset < offset + size)
      if (overlap) problem(`${where}: field ${id} (${offset}..${offset + size! - 1}) overlaps ${overlap.id} (${overlap.offset}..${overlap.offset + overlap.size! - 1}).`)
      const enumGroup = str(f.enum)
      if (enumGroup && !this.enums[enumGroup]) problem(`${where}: field ${id} names enum ${enumGroup}, which enums.json does not have.`)
      const choices = this.choices(f.options, f.type === 'enum' ? enumGroup : undefined)
      fields.push({
        id,
        label: str(f.label) ?? str(f.alias) ?? id,
        ...(str(f.description) ? { description: str(f.description) } : {}),
        type: str(f.type) ?? 'integer',
        rawType,
        offset,
        ...(size !== undefined ? { size } : {}),
        ...(str(f.unit) ? { unit: str(f.unit) } : {}),
        ...(num(f.min) !== undefined ? { min: num(f.min) } : {}),
        ...(num(f.max) !== undefined ? { max: num(f.max) } : {}),
        ...(num(f.step) !== undefined ? { step: num(f.step) } : {}),
        ...(f.default !== undefined ? { default: f.default } : {}),
        optional: f.optional === true,
        ...(choices ? { choices } : {}),
        ...(enumGroup ? { enum: enumGroup } : {}),
        ...(str(f.requires_contract) ? { requiresContract: str(f.requires_contract) } : {}),
        ...(str(f.requires) ? { requires: str(f.requires) } : {}),
        pinRef: f.pin_ref === true,
      })
      if (size === undefined) variable = true
      else {
        cursor = offset + size
        fixed = Math.max(fixed, cursor)
      }
    }
    if (declared !== undefined && !variable && declared !== fixed) problem(`${where}: the fields take ${fixed} bytes, the struct is ${declared}.`)
    return { ...(str(json.struct) ? { struct: str(json.struct) } : {}), fields, fixed: declared !== undefined && !variable ? Math.max(declared, fixed) : fixed, variable }
  }
}
