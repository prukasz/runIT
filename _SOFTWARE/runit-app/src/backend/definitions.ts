import enumsJson from '@generated/common/enums.json'
import errorsJson from '@generated/common/errors.json'
import boardJson from '@generated/common/board.json'
import devkitBoardJson from '@generated/common/board_devkit.json'
import actionsContract from '@generated/services/contract_actions.json'
import projectContract from '@generated/services/contract_project.json'
import streamContract from '@generated/services/contract_stream.json'
import type { EnumTables } from '@engine/generated/wire'
import { parseRanges } from './catalog/codec'
import type { FieldRanges } from './catalog/codec'
import { ServiceCatalog } from './catalog/services'
import type { DescriptorFile } from './catalog/services'

/*
 * The one file of the backend that reads generated/. The numbers of the link (generated/common): the router's keys, the
 * direction byte, flags,
 * status, the board's own streams (`stream`) and the session's channels (`session_channel`), the session's frame byte, kinds,
 * control ops, ids and version (`enums.json`, groups `session_*`, `sys_session_*`), the runIT GATT UUIDs and the fragment header bits of `sys_ble` (`enums.json`,
 * groups `ble_runit_uuid` and `ble_frag`), the largest frame (`enums.json`, group `cmd_frame`), the errors (`errors.json`: tags, fields, message
 * templates, what an id field refers to) and the enum groups their fields name (`bundledEnums`); and every
 * command of every router (the service, contract and device descriptors, `bundledServiceCatalog`). Nothing here is a number
 * typed in by hand; a symbol the firmware stops publishing stops the load with a message that names it.
 */

export interface LinkDefinitions {
  /** Root keys by name (`sys`, `ble`, `vm`, ...): the first key of a request after the session's header. */
  readonly keys: Readonly<Record<string, number>>
  /** The byte after the root key of every request: the side of the command it runs (`cmd_dir_e`). */
  readonly dir: { readonly read: number; readonly write: number }
  /** The board's own streams (`stream`, SYS_STREAM.MD "Model"): the first byte of a PUSH frame's body below the channels. */
  readonly streams: { readonly telemetry: number; readonly logs: number; readonly errors: number; readonly events: number }
  /**
   * The session's own channels (`session_channel`, SYS_SESSION.MD "Streams"): a PUSH frame's first byte from `first` on is
   * a channel, not a stream: a watch's entries (`watches`), a remote device's events and errors (`remote`), data for a provider (`data`: `[to id][from id][data]`).
   */
  readonly channels: { readonly first: number; readonly watches: number; readonly remote: number; readonly data: number }
  /**
   * A stream as STREAM CREATE and LIST have it (SYS_STREAM.MD "Model"): its direction (`sys_stream_dir_e`), its flag, the
   * state bit of the board's own, the kinds of its targets (`sys_stream_target_e`) and the ids a user stream may take.
   */
  readonly stream: {
    readonly dir: { readonly out: number; readonly in: number }
    readonly flagUrgent: number
    readonly stateSystem: number
    readonly target: { readonly provider: number; readonly peer: number; readonly callback: number; readonly output: number; readonly remote: number }
    readonly userFirst: number
    readonly last: number
  }
  readonly status: { readonly ok: number; readonly error: number }
  readonly flags: { readonly urgent: number; readonly noReply: number }
  /** The session (SYS_SESSION.MD "Frames"): the frame byte's bits, the kinds, the control ops, the ids, the version. */
  readonly session: {
    readonly version: number
    readonly frame: { readonly flags: number; readonly status: number; readonly kind: number }
    readonly kind: { readonly request: number; readonly answer: number; readonly push: number; readonly control: number }
    readonly control: { readonly hello: number; readonly ping: number; readonly noSession: number; readonly gone: number }
    /** What a session's peer is to the board (`sys_session_peer_e`): its master, a slave, a client. */
    readonly peer: { readonly master: number; readonly slave: number; readonly client: number }
    /** `app`: the id the app says in its hello (`SYS_SESSION_ID_APP`); other clients pick one in `clientFirst..clientLast`. */
    readonly id: {
      readonly none: number; readonly self: number; readonly broadcast: number; readonly app: number
      readonly clientFirst: number; readonly clientLast: number; readonly boardLast: number
    }
  }
  readonly gatt: { readonly service: number; readonly tx: number; readonly rx: number; readonly logs: number; readonly status: number }
  readonly frag: { readonly last: number; readonly first: number; readonly countMask: number }
  /** The error record's header (ERRORS.MD "Error record"). */
  readonly errorRecord: { readonly srcInternal: number; readonly srcCommand: number; readonly srcBoard: number; readonly requestCut: number; readonly stampBytes: number }
  /** The ops of the `actions`, `project` and `stream` routers by command id (`RUN`, `BEGIN`, `SUBSCRIBE`, ...): `generated/services/contract_*.json`. */
  readonly ops: { readonly actions: Readonly<Record<string, number>>; readonly project: Readonly<Record<string, number>>; readonly stream: Readonly<Record<string, number>> }
  /** The direction byte each of those ops is sent with: its write, or its read when it has no write (`INFO`, `READ`, `LIST`). */
  readonly opDirs: { readonly actions: Readonly<Record<string, number>>; readonly project: Readonly<Record<string, number>>; readonly stream: Readonly<Record<string, number>> }
  /** `CMD_FRAME_MAX` (cmd.h): the largest frame, the one limit of every session's link. */
  readonly frameMax: number
  /** `CMD_HEAD` (cmd.h): the room a frame keeps for its header; a request (keys and payload) is at most `frameMax - frameHead`. */
  readonly frameHead: number
  /** The op of an event frame on the events stream (`SYS_EVENT_OP_NOTIFY`, group `event_op`): `[event key][NOTIFY][sys_event_t]`. */
  readonly eventNotify: number
  /** What the board does with the stored project when it starts and right after a save (`sys_project_boot_e`). */
  readonly projectBoot: { readonly none: number; readonly load: number; readonly run: number }
}

const value = (enums: EnumTables, group: string, symbol: string): number => {
  const table = enums[group]
  if (!table) throw new Error(`generated/common/enums.json has no group '${group}'.`)
  const member = table.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`Group '${group}' of generated/common/enums.json has no member '${symbol}'.`)
  return member.value
}

interface ContractFile {
  readonly commands: readonly { readonly id: string; readonly op: number; readonly write?: unknown }[]
}

const opsOf = (contract: ContractFile): Record<string, number> => Object.fromEntries(contract.commands.map((c) => [c.id, c.op]))
const opDirsOf = (contract: ContractFile, dir: LinkDefinitions['dir']): Record<string, number> => Object.fromEntries(contract.commands.map((c) => [c.id, c.write ? dir.write : dir.read]))

export const linkDefinitions = (
  enums: EnumTables,
  contracts: { readonly actions: ContractFile; readonly project: ContractFile; readonly stream: ContractFile } = {
    actions: actionsContract as ContractFile, project: projectContract as ContractFile, stream: streamContract as ContractFile,
  },
): LinkDefinitions => {
  const keys = enums.router_key
  if (!keys) throw new Error("generated/common/enums.json has no group 'router_key'.")
  const dir = { read: value(enums, 'cmd_dir_e', 'CMD_DIR_READ'), write: value(enums, 'cmd_dir_e', 'CMD_DIR_WRITE') }
  return {
    keys: Object.fromEntries(keys.members.map((m) => [m.name.replace(/^ROUTER_KEY_/, '').toLowerCase(), m.value])),
    dir,
    streams: {
      telemetry: value(enums, 'stream', 'SYS_STREAM_TELEMETRY'),
      logs: value(enums, 'stream', 'SYS_STREAM_LOGS'),
      errors: value(enums, 'stream', 'SYS_STREAM_ERRORS'),
      events: value(enums, 'stream', 'SYS_STREAM_EVENTS'),
    },
    channels: {
      first: value(enums, 'stream_range', 'SYS_STREAM_ID_LAST') + 1,
      watches: value(enums, 'session_channel', 'SYS_SESSION_CHANNEL_WATCHES'),
      remote: value(enums, 'session_channel', 'SYS_SESSION_CHANNEL_REMOTE'),
      data: value(enums, 'session_channel', 'SYS_SESSION_CHANNEL_DATA'),
    },
    stream: {
      dir: { out: value(enums, 'sys_stream_dir_e', 'SYS_STREAM_OUT'), in: value(enums, 'sys_stream_dir_e', 'SYS_STREAM_IN') },
      flagUrgent: value(enums, 'stream_flag', 'SYS_STREAM_FLAG_URGENT'),
      stateSystem: value(enums, 'stream_state', 'SYS_STREAM_STATE_SYSTEM'),
      target: {
        provider: value(enums, 'sys_stream_target_e', 'SYS_STREAM_TARGET_PROVIDER'),
        peer: value(enums, 'sys_stream_target_e', 'SYS_STREAM_TARGET_PEER'),
        callback: value(enums, 'sys_stream_target_e', 'SYS_STREAM_TARGET_CALLBACK'),
        output: value(enums, 'sys_stream_target_e', 'SYS_STREAM_TARGET_OUTPUT'),
        remote: value(enums, 'sys_stream_target_e', 'SYS_STREAM_TARGET_REMOTE'),
      },
      userFirst: value(enums, 'stream_range', 'SYS_STREAM_ID_USER_FIRST'),
      last: value(enums, 'stream_range', 'SYS_STREAM_ID_LAST'),
    },
    status: { ok: value(enums, 'router_status_e', 'ROUTER_STATUS_OK'), error: value(enums, 'router_status_e', 'ROUTER_STATUS_ERROR') },
    flags: { urgent: value(enums, 'router_flag', 'ROUTER_FLAG_URGENT'), noReply: value(enums, 'router_flag', 'ROUTER_FLAG_NOREPLY') },
    session: {
      version: value(enums, 'session_version', 'SYS_SESSION_VERSION'),
      frame: {
        flags: value(enums, 'session_frame', 'SYS_SESSION_FRAME_FLAGS'),
        status: value(enums, 'session_frame', 'SYS_SESSION_FRAME_STATUS'),
        kind: value(enums, 'session_frame', 'SYS_SESSION_FRAME_KIND'),
      },
      kind: {
        request: value(enums, 'sys_session_kind_e', 'SYS_SESSION_REQUEST'),
        answer: value(enums, 'sys_session_kind_e', 'SYS_SESSION_ANSWER'),
        push: value(enums, 'sys_session_kind_e', 'SYS_SESSION_PUSH'),
        control: value(enums, 'sys_session_kind_e', 'SYS_SESSION_CONTROL'),
      },
      control: {
        hello: value(enums, 'sys_session_control_e', 'SYS_SESSION_HELLO'),
        ping: value(enums, 'sys_session_control_e', 'SYS_SESSION_PING'),
        noSession: value(enums, 'sys_session_control_e', 'SYS_SESSION_NO_SESSION'),
        gone: value(enums, 'sys_session_control_e', 'SYS_SESSION_GONE'),
      },
      peer: {
        master: value(enums, 'sys_session_peer_e', 'SYS_SESSION_MASTER'),
        slave: value(enums, 'sys_session_peer_e', 'SYS_SESSION_SLAVE'),
        client: value(enums, 'sys_session_peer_e', 'SYS_SESSION_CLIENT'),
      },
      id: {
        none: value(enums, 'session_id', 'SYS_SESSION_ID_NONE'),
        self: value(enums, 'session_id', 'SYS_SESSION_ID_SELF'),
        broadcast: value(enums, 'session_id', 'SYS_SESSION_ID_BROADCAST'),
        app: value(enums, 'session_id', 'SYS_SESSION_ID_APP'),
        clientFirst: value(enums, 'session_id', 'SYS_SESSION_ID_CLIENT_FIRST'),
        clientLast: value(enums, 'session_id', 'SYS_SESSION_ID_CLIENT_LAST'),
        boardLast: value(enums, 'session_id', 'SYS_SESSION_ID_LAST'),
      },
    },
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
      srcInternal: value(enums, 'err_record_src_e', 'ERR_RECORD_SRC_INTERNAL'),
      srcCommand: value(enums, 'err_record_src_e', 'ERR_RECORD_SRC_COMMAND'),
      srcBoard: value(enums, 'err_record_src_e', 'ERR_RECORD_SRC_BOARD'),
      requestCut: value(enums, 'err_record_flag', 'ERR_RECORD_REQ_CUT'),
      stampBytes: value(enums, 'err_record_flag', 'ERR_RECORD_STAMP_BYTES'),
    },
    ops: { actions: opsOf(contracts.actions), project: opsOf(contracts.project), stream: opsOf(contracts.stream) },
    opDirs: { actions: opDirsOf(contracts.actions, dir), project: opDirsOf(contracts.project, dir), stream: opDirsOf(contracts.stream, dir) },
    frameMax: value(enums, 'cmd_frame', 'CMD_FRAME_MAX'),
    frameHead: value(enums, 'cmd_frame', 'CMD_HEAD'),
    eventNotify: value(enums, 'event_op', 'SYS_EVENT_OP_NOTIFY'),
    projectBoot: {
      none: value(enums, 'sys_project_boot_e', 'SYS_PROJECT_BOOT_NONE'),
      load: value(enums, 'sys_project_boot_e', 'SYS_PROJECT_BOOT_LOAD'),
      run: value(enums, 'sys_project_boot_e', 'SYS_PROJECT_BOOT_RUN'),
    },
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
  /** The pins of other devices it uses, by what for (`intr_pin`, `in_pin[0]`); `slot`: its place in a config field that is a list of pins. */
  readonly pins: readonly (BoardPin & { readonly use: string; readonly slot?: number; readonly description?: string })[]
  /** The config values board.json gives (the others are the descriptor's defaults). */
  readonly config: Readonly<Record<string, unknown>>
  /** Where it sits on an I2C bus of the board (`i2c_buses`). */
  readonly i2c?: { readonly bus: number; readonly address: number }
  /** The ranges board.json gives for the fields of its commands (`COMMAND.field`); they replace the device type's. */
  readonly ranges?: FieldRanges
}

/** What board.json says of the board: its devices, the pins set up at boot and the pins nothing else may take. */
export interface BoardDescriptor {
  readonly name: string
  /** The board's revision (board.json): with the name, which drawing of the hardware library is this board. */
  readonly revision: number
  readonly devices: readonly BoardDevice[]
  readonly i2cBuses: readonly { readonly bus: number; readonly name: string; readonly sda: BoardPin; readonly scl: BoardPin; readonly pullup: boolean; readonly description?: string }[]
  /** The pins the board's profile sets up: their `sys_io_mode_e` member name, an output's level and what they are for. */
  readonly pinSetup: readonly (BoardPin & { readonly label: string; readonly mode: string; readonly level?: boolean; readonly description?: string })[]
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
  /** The limits of the board's input (`power.limits` of board.json), which its profile gives the power manager. */
  readonly limits: { readonly max_mV: number; readonly max_mA: number; readonly reserve_mW: number; readonly unknown_source_mA: number; readonly vreg_efficiency_pct: number }
  readonly input?: PowerProbe & { readonly description?: string }
  /** The USB-PD sink, when the board has one. */
  readonly usbPd?: number
  /** The eFuse power-good pins of the two inputs, when the board has them. */
  readonly sourcePins?: { readonly usbOk: BoardPin; readonly extOk: BoardPin }
  readonly rails: readonly { readonly id: string; readonly name: string; readonly regulator: number; readonly monitor?: PowerProbe; readonly min_mA: number; readonly description?: string }[]
}

/**
 * The boards the app knows: the runIT PCB, or a plain ESP32-S3 devkit with only the ESP pins (no chips, no rails). The firmware
 * is the same for both; the app writes the board's profile from its descriptor (`app/devices/boardProfile.ts`).
 */
export type BoardKind = 'pcb' | 'devkit'

/** The board this app was started for: `VITE_RUNIT_BOARD` (`npm run dev:devkit`, `npm run app:devkit`), else the PCB. */
export const appBoardKind = (): BoardKind => (import.meta.env.VITE_RUNIT_BOARD === 'devkit' ? 'devkit' : 'pcb')

/** A bundled board (generated/common/board.json, board_devkit.json); by default the one the app was started for. */
export const bundledBoard = (kind: BoardKind = appBoardKind()): BoardDescriptor => {
  const json = (kind === 'devkit' ? devkitBoardJson : boardJson) as unknown as {
    name: string
    revision: number
    devices: { id: number; name: string; description?: string; descriptor: string; ranges?: unknown; pins?: { use: string; slot?: number; device: number; pin: number; description?: string }[]; config?: Record<string, unknown>; i2c?: { bus: number; address: number } }[]
    i2c_buses?: { bus: number; name: string; sda_pin: BoardPin; scl_pin: BoardPin; pullup?: boolean; description?: string }[]
    pin_setup?: { device: number; pin: number; mode: string; level?: boolean; label: string; description?: string }[]
    reserved_pins?: { device: number; pin: number; label: string; reason?: string }[]
    power: {
      limits: BoardPower['limits']
      sources?: { usb_pd?: { device?: number; ok_pin?: BoardPin }; external?: { ok_pin?: BoardPin } }
      input_monitor?: { device: number; channel: number; description?: string }
      rails?: { id: string; name: string; vreg_device: number; monitor?: { device: number; channel: number }; min_mA: number; description?: string }[]
    }
  }
  const usbOk = json.power.sources?.usb_pd?.ok_pin
  const extOk = json.power.sources?.external?.ok_pin
  const usbPd = json.power.sources?.usb_pd?.device
  return {
    name: json.name,
    revision: json.revision,
    devices: json.devices.map((d) => ({ id: d.id, name: d.name, ...(d.description ? { description: d.description } : {}), descriptor: d.descriptor, pins: d.pins ?? [], config: d.config ?? {}, ...(d.i2c ? { i2c: { bus: d.i2c.bus, address: d.i2c.address } } : {}), ...(Object.keys(parseRanges(d.ranges)).length > 0 ? { ranges: parseRanges(d.ranges) } : {}) })),
    i2cBuses: (json.i2c_buses ?? []).map((b) => ({ bus: b.bus, name: b.name, sda: b.sda_pin, scl: b.scl_pin, pullup: b.pullup ?? false, ...(b.description ? { description: b.description } : {}) })),
    pinSetup: json.pin_setup ?? [],
    reservedPins: json.reserved_pins ?? [],
    power: {
      limits: json.power.limits,
      ...(json.power.input_monitor ? { input: json.power.input_monitor } : {}),
      ...(usbPd !== undefined ? { usbPd } : {}),
      ...(usbOk && extOk ? { sourcePins: { usbOk, extOk } } : {}),
      rails: (json.power.rails ?? []).map((r) => ({ id: r.id, name: r.name, regulator: r.vreg_device, ...(r.monitor ? { monitor: r.monitor } : {}), min_mA: r.min_mA, ...(r.description ? { description: r.description } : {}) })),
    },
  }
}

/** The firmware's own actions (`onboard_action` in `enums.json`, `SYS_ACTIONS_ONBOARD_*` in `sys_actions.h`): the board writes them at every boot, user class. */
export interface OnboardAction {
  readonly id: number
  /** The name the firmware gives it, e.g. `Suspend all`. */
  readonly alias: string
}

/** The user-class record IDs the board writes at every boot: an app stores its own actions under other IDs, or the next boot replaces them. */
export const onboardActions = (): readonly OnboardAction[] =>
  (bundledEnums().onboard_action?.members ?? []).map((m) => ({ id: m.value, alias: m.alias ?? m.name }))

export const onboardActionIds = (): ReadonlySet<number> => new Set(onboardActions().map((a) => a.id))

/** The definitions of the bundled firmware descriptors. */
export const bundledLinkDefinitions = (): LinkDefinitions => linkDefinitions(bundledEnums())

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
