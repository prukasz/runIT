import enumsJson from '@generated/common/enums.json'
import errorsJson from '@generated/common/errors.json'
import limitsJson from '@generated/common/limits.json'
import boardJson from '@generated/common/board.json'
import devkitBoardJson from '@generated/common/board_devkit.json'
import actionsContract from '@generated/services/contract_actions.json'
import projectContract from '@generated/services/contract_project.json'
import type { EnumTables } from '@engine/generated/wire'
import { ServiceCatalog } from './catalog/services'
import type { DescriptorFile } from './catalog/services'

/*
 * The one file of the backend that reads generated/. The numbers of the link (generated/common): the router's keys, flags,
 * status and the stream bytes (`enums.json`), the runIT GATT UUIDs and the fragment header bits of `sys_ble` (`enums.json`,
 * groups `ble_runit_uuid` and `ble_frag`), the largest frame (`limits.json`), the errors (`errors.json`: tags, fields, message
 * templates, what an id field refers to) and the enum groups their fields name (`bundledEnums`); and every
 * command of every router (the service, contract and device descriptors, `bundledServiceCatalog`). Nothing here is a number
 * typed in by hand; a symbol the firmware stops publishing stops the load with a message that names it.
 */

export interface LinkDefinitions {
  /** Root keys by name (`sys`, `ble`, `vm`, ...): the first key of a request after `[seq][flags]`. */
  readonly keys: Readonly<Record<string, number>>
  /** The first byte of a frame says which stream it belongs to. */
  readonly streams: { readonly response: number; readonly telemetry: number; readonly logs: number; readonly errors: number; readonly events: number }
  readonly status: { readonly ok: number; readonly error: number }
  readonly flags: { readonly urgent: number }
  readonly gatt: { readonly service: number; readonly tx: number; readonly rx: number; readonly logs: number; readonly status: number }
  readonly frag: { readonly last: number; readonly first: number; readonly countMask: number }
  /** The error record's header (ROUTER.MD "Error record"). */
  readonly errorRecord: { readonly srcInternal: number; readonly srcCommand: number; readonly requestCut: number; readonly stampBytes: number }
  /** The ops of the `actions` and `project` routers by command id (`RUN`, `BEGIN`, ...): `generated/services/contract_*.json`. */
  readonly ops: { readonly actions: Readonly<Record<string, number>>; readonly project: Readonly<Record<string, number>> }
  /** `CONFIG_ROUTER_FRAME_MAX`: the largest request (and response) frame. A link may carry less. */
  readonly frameMax: number
  /** The op of an event frame on the events stream (`SYS_EVENT_OP_NOTIFY`, group `event_op`): `[event key][NOTIFY][sys_event_t]`. */
  readonly eventNotify: number
}

const value = (enums: EnumTables, group: string, symbol: string): number => {
  const table = enums[group]
  if (!table) throw new Error(`generated/common/enums.json has no group '${group}'.`)
  const member = table.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`Group '${group}' of generated/common/enums.json has no member '${symbol}'.`)
  return member.value
}

interface ContractFile {
  readonly commands: readonly { readonly id: string; readonly op: number }[]
}

const opsOf = (contract: ContractFile): Record<string, number> => Object.fromEntries(contract.commands.map((c) => [c.id, c.op]))

export const linkDefinitions = (
  enums: EnumTables,
  frameMax: number,
  contracts: { readonly actions: ContractFile; readonly project: ContractFile } = { actions: actionsContract as ContractFile, project: projectContract as ContractFile },
): LinkDefinitions => {
  const keys = enums.router_key
  if (!keys) throw new Error("generated/common/enums.json has no group 'router_key'.")
  return {
    keys: Object.fromEntries(keys.members.map((m) => [m.name.replace(/^ROUTER_KEY_/, '').toLowerCase(), m.value])),
    streams: {
      response: value(enums, 'stream_byte', 'SYS_CONNECTOR_STREAM_RESPONSE'),
      telemetry: value(enums, 'stream_byte', 'SYS_CONNECTOR_STREAM_TELEMETRY'),
      logs: value(enums, 'stream_byte', 'SYS_CONNECTOR_STREAM_LOGS'),
      errors: value(enums, 'stream_byte', 'SYS_CONNECTOR_STREAM_ERRORS'),
      events: value(enums, 'stream_byte', 'SYS_CONNECTOR_STREAM_EVENTS'),
    },
    status: { ok: value(enums, 'router_status_e', 'ROUTER_STATUS_OK'), error: value(enums, 'router_status_e', 'ROUTER_STATUS_ERROR') },
    flags: { urgent: value(enums, 'router_flag', 'ROUTER_FLAG_URGENT') },
    gatt: {
      service: value(enums, 'ble_runit_uuid', 'SYS_BLE_SVC_RUNIT'),
      tx: value(enums, 'ble_runit_uuid', 'SYS_BLE_CHR_RUNIT_TX'),
      rx: value(enums, 'ble_runit_uuid', 'SYS_BLE_CHR_RUNIT_RX'),
      logs: value(enums, 'ble_runit_uuid', 'SYS_BLE_CHR_RUNIT_LOGS'),
      status: value(enums, 'ble_runit_uuid', 'SYS_BLE_CHT_RUNIT_STATUS'),
    },
    frag: {
      last: value(enums, 'ble_frag', 'SYS_BLE_FRAG_LAST'),
      first: value(enums, 'ble_frag', 'SYS_BLE_FRAG_FIRST'),
      countMask: value(enums, 'ble_frag', 'SYS_BLE_FRAG_COUNT_MASK'),
    },
    errorRecord: {
      srcInternal: value(enums, 'router_err_src_e', 'ROUTER_ERR_SRC_INTERNAL'),
      srcCommand: value(enums, 'router_err_src_e', 'ROUTER_ERR_SRC_COMMAND'),
      requestCut: value(enums, 'router_err_flag', 'ROUTER_ERR_REQ_CUT'),
      stampBytes: value(enums, 'router_err_flag', 'ROUTER_ERR_STAMP_BYTES'),
    },
    ops: { actions: opsOf(contracts.actions), project: opsOf(contracts.project) },
    frameMax,
    eventNotify: value(enums, 'event_op', 'SYS_EVENT_OP_NOTIFY'),
  }
}

/* ---- the board ---- */

/** A pin of a device as board.json names it. */
export interface BoardPin {
  readonly device: number
  readonly pin: number
}

/** A device the firmware creates at boot (board.json `devices`): the board's own, which a project's change leaves (SYS_DEVICE.MD). */
export interface BoardDevice {
  readonly id: number
  readonly name: string
  readonly description?: string
  /** The device descriptor's file name (`device_tca6424a`). */
  readonly descriptor: string
  /** The pins of other devices it uses, by what for (`intr_pin`, `in_pin[0]`). */
  readonly pins: readonly (BoardPin & { readonly use: string; readonly description?: string })[]
  /** The config values board.json gives (the others are the descriptor's defaults). */
  readonly config: Readonly<Record<string, unknown>>
  /** Where it sits on an I2C bus of the board (`i2c_buses`). */
  readonly i2c?: { readonly bus: number; readonly address: number }
}

/** What board.json says of the board: its devices, the pins set up at boot and the pins nothing else may take. */
export interface BoardDescriptor {
  readonly name: string
  readonly devices: readonly BoardDevice[]
  readonly i2cBuses: readonly { readonly bus: number; readonly name: string; readonly description?: string }[]
  /** The pins the boot sets up: their `sys_io_mode_e` member name and what they are for. */
  readonly pinSetup: readonly (BoardPin & { readonly label: string; readonly mode: string; readonly description?: string })[]
  readonly reservedPins: readonly (BoardPin & { readonly label: string; readonly reason?: string })[]
  /** Where the board's power is measured (`power` of board.json): the whole input and each regulator rail, by a power monitor's channel. */
  readonly power: BoardPower
}

/** A power monitor's channel (`POWER_MONITOR` commands take the channel). */
export interface PowerProbe {
  readonly device: number
  readonly channel: number
}

export interface BoardPower {
  readonly input?: PowerProbe & { readonly description?: string }
  readonly rails: readonly { readonly id: string; readonly name: string; readonly regulator: number; readonly monitor?: PowerProbe; readonly description?: string }[]
}

/**
 * The boards the firmware is built for (`CONFIG_RUNIT_BOARD`): the runIT PCB, or a plain ESP32-S3 devkit with only the ESP pins
 * (no chips, no power layout). The app is the same for both; only the board descriptor differs.
 */
export type BoardKind = 'pcb' | 'devkit'

/** The board this app was started for: `VITE_RUNIT_BOARD` (`npm run dev:devkit`, `npm run app:devkit`), else the PCB. */
export const appBoardKind = (): BoardKind => (import.meta.env.VITE_RUNIT_BOARD === 'devkit' ? 'devkit' : 'pcb')

/** A bundled board (generated/common/board.json, board_devkit.json); by default the one the app was started for. */
export const bundledBoard = (kind: BoardKind = appBoardKind()): BoardDescriptor => {
  const json = (kind === 'devkit' ? devkitBoardJson : boardJson) as unknown as {
    name: string
    devices: { id: number; name: string; description?: string; descriptor: string; pins?: { use: string; device: number; pin: number; description?: string }[]; config?: Record<string, unknown>; i2c?: { bus: number; address: number } }[]
    i2c_buses?: { bus: number; name: string; description?: string }[]
    pin_setup?: { device: number; pin: number; mode: string; label: string; description?: string }[]
    reserved_pins?: { device: number; pin: number; label: string; reason?: string }[]
    power?: {
      input_monitor?: { device: number; channel: number; description?: string }
      rails?: { id: string; name: string; vreg_device: number; monitor?: { device: number; channel: number }; description?: string }[]
    }
  }
  return {
    name: json.name,
    devices: json.devices.map((d) => ({ id: d.id, name: d.name, ...(d.description ? { description: d.description } : {}), descriptor: d.descriptor, pins: d.pins ?? [], config: d.config ?? {}, ...(d.i2c ? { i2c: { bus: d.i2c.bus, address: d.i2c.address } } : {}) })),
    i2cBuses: json.i2c_buses ?? [],
    pinSetup: json.pin_setup ?? [],
    reservedPins: json.reserved_pins ?? [],
    power: {
      ...(json.power?.input_monitor ? { input: json.power.input_monitor } : {}),
      rails: (json.power?.rails ?? []).map((r) => ({ id: r.id, name: r.name, regulator: r.vreg_device, ...(r.monitor ? { monitor: r.monitor } : {}), ...(r.description ? { description: r.description } : {}) })),
    },
  }
}

/**
 * The user-class record IDs the firmware writes at every boot (its onboard actions, `RUNIT_ACTION_ID_*` in limits.json, RUNIT.MD):
 * an app stores its own actions under other IDs, or the next boot replaces them.
 */
/** The firmware's own actions (`RUNIT_ACTION_ID_*` in `limits.json`, `Onboard action: …` in the Kconfig): the board writes them at every boot. */
export interface OnboardAction {
  readonly id: number
  /** The name the firmware gives it, e.g. `Onboard action: suspend all`. */
  readonly alias: string
}

export const onboardActions = (): readonly OnboardAction[] =>
  Object.entries((limitsJson as unknown as { limits: Record<string, { value: number; alias?: string }> }).limits)
    .filter(([name]) => name.startsWith('RUNIT_ACTION_ID_'))
    .map(([name, limit]) => ({ id: limit.value, alias: limit.alias ?? name }))

export const onboardActionIds = (): ReadonlySet<number> => new Set(onboardActions().map((a) => a.id))

/** The definitions of the bundled firmware descriptors. */
export const bundledLinkDefinitions = (): LinkDefinitions => {
  const limit = (limitsJson as unknown as { limits: Record<string, { value: number }> }).limits.ROUTER_FRAME_MAX
  if (!limit) throw new Error("generated/common/limits.json has no limit 'ROUTER_FRAME_MAX'.")
  return linkDefinitions(bundledEnums(), limit.value)
}

/* ---- every command ---- */

/*
 * The descriptors of the bundle: every system service, device contract and device type. Vite resolves the globs at build time, so
 * a new `contract_*.json` / `device_*.json` in generated/ is picked up with nothing to edit (the rule of
 * templates/generate_bundle.py).
 */
const descriptorFiles = (modules: Record<string, unknown>): DescriptorFile[] =>
  Object.keys(modules)
    .sort()
    .map((path) => ({ source: path.replace(/^.*\/generated\//, 'generated/'), json: modules[path] }))

/** The catalog of every command the bundled firmware descriptors describe. */
export const bundledServiceCatalog = (): ServiceCatalog =>
  new ServiceCatalog(
    [
      ...descriptorFiles(import.meta.glob('@generated/services/contract_*.json', { eager: true, import: 'default' })),
      ...descriptorFiles(import.meta.glob('@generated/contracts/contract_*.json', { eager: true, import: 'default' })),
      ...descriptorFiles(import.meta.glob('@generated/devices/device_*.json', { eager: true, import: 'default' })),
    ],
    bundledEnums(),
  )

/** VM block types by wire id -> their name (`alias` of each `generated/blocks/block_*.json`). */
export const bundledBlockTypes = (): ReadonlyMap<number, string> =>
  new Map(
    Object.values(import.meta.glob('@generated/blocks/block_*.json', { eager: true, import: 'default' }) as Record<string, { wire_id?: number; alias?: string; id: string }>)
      .filter((b) => typeof b.wire_id === 'number')
      .map((b) => [b.wire_id!, b.alias ?? b.id]),
  )

/** Every enum group the firmware publishes (`enums.json`): the members an `enum_ref` of an error field names. */
export const bundledEnums = (): EnumTables => (enumsJson as unknown as { enums: EnumTables }).enums

/* ---- errors ---- */

export interface ErrorField {
  readonly name: string
  readonly type: string
  readonly offset: number
  readonly size: number
  /** What the number refers to (`id_kinds`): a device, a pin, a variable, a block, ... */
  readonly id_kind?: string
  /** What it refers to, by the value of another field: `kinds` by that value. */
  readonly id_kind_by?: { readonly field: string; readonly kinds: Readonly<Record<string, string>> }
  /** For a kind with a scope: the field of the same error that holds the instance (the device of a pin). */
  readonly of?: string
  /** The enum group of `enums.json` the value is a member of. */
  readonly enum_ref?: string
  readonly unit?: string
}

export interface ErrorInfo {
  readonly tag: number
  readonly name: string
  readonly module: string
  readonly severity: number
  readonly description: string
  /** The template a user reads: `{field}` is the value of a field. */
  readonly message: string
  /** What a user can do about it (a template like `message`), when errors.json has one. */
  readonly hint?: string
  /** A node that says where in the chain the error is (a route step, a device, an action), not what went wrong. */
  readonly context: boolean
  readonly fields: readonly ErrorField[]
}

/** What an id field can refer to (`id_kinds` of errors.json). */
export interface IdKindInfo {
  readonly alias: string
  readonly description?: string
  /** It is numbered within an instance of this kind (a pin within a device). */
  readonly scope?: string
  /** `hex`: the number is shown in hex when there is no name for it. */
  readonly format?: string
}

interface ErrorsFile {
  readonly severities: Readonly<Record<string, number>>
  readonly id_kinds: Readonly<Record<string, IdKindInfo>>
  readonly modules: readonly {
    readonly name: string
    readonly tags: readonly { readonly tag: number; readonly name: string; readonly severity: number; readonly description?: string; readonly message: string; readonly hint?: string; readonly context?: boolean; readonly fields: readonly ErrorField[] }[]
  }[]
}

const bundledErrors = (): ErrorsFile => errorsJson as unknown as ErrorsFile

/** Every error the firmware can raise, by tag (`errors.json`). */
export const errorCatalog = (file: ErrorsFile = bundledErrors()): ReadonlyMap<number, ErrorInfo> =>
  new Map(
    file.modules.flatMap((m) =>
      m.tags.map((t): [number, ErrorInfo] => [
        t.tag,
        { tag: t.tag, name: t.name, module: m.name, severity: t.severity, description: t.description ?? '', message: t.message, ...(t.hint ? { hint: t.hint } : {}), context: t.context === true, fields: t.fields },
      ]),
    ),
  )

/** What an error's id fields can refer to, by kind (`id_kinds` of errors.json). */
export const errorIdKinds = (file: ErrorsFile = bundledErrors()): ReadonlyMap<string, IdKindInfo> => new Map(Object.entries(file.id_kinds))

/** The severities by symbol (`ERR_SEV_NONE` ... `ERR_SEV_CRITICAL`). */
export const errorSeverities = (file: ErrorsFile = bundledErrors()): Readonly<Record<string, number>> => file.severities
