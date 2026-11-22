import enumsJson from '@generated/common/enums.json'
import errorsJson from '@generated/common/errors.json'
import type { EnumTables } from '@engine/generated/wire'
import { ServiceCatalog } from './catalog/services'
import type { DescriptorFile } from './catalog/services'

/*
 * The one file of the backend that reads generated/. Every number of the link is a symbol the firmware publishes in
 * `enums.json`: the header of every frame (`cmd_frame`, `cmd_type`, `cmd_kind_e`, `cmd_src`: cmd.h), the direction byte
 * (`cmd_dir_e`), the router's flags and status, the session's control ops (`sys_session_control_e`), the module numbers that
 * are a request's key and a PUSH frame's stream (`module_id`), the runIT GATT UUIDs and the fragment bits (`ble_runit_uuid`,
 * `ble_frag`), the flags of the board's GATT table and of a BLE provider (`ble_svc_flag`, `ble_chr_flag`, `provider_ble_flag`,
 * `cmd_send_flag`), the error record's header (`err_record_*`); then the errors (`errors.json`) and every command of every
 * router, device type and provider type (`bundledServiceCatalog`). Nothing here is a number typed in by hand: a symbol the
 * firmware stops publishing stops the load with a message that names it.
 */

/** The streams a board pushes, by the module whose number is their key (DATA_MATRIX.MD, RUNIT.MD "The board's data"). */
export const STREAM_MODULES = {
  /** Error records (ERRORS.MD "Error record"): the session's. */
  errors: 'MODULE_ID_ERRORS',
  /** `sys_event_t`s (SYS_EVENT_STREAM.MD): the session's. */
  events: 'MODULE_ID_SYS_EVENT',
  /** The log lines as text: the raw logs characteristic. */
  logs: 'MODULE_ID_LOG',
  /** The watches' entries `[owner][id][status][data]` (SYS_WATCH.MD): the raw telemetry characteristic. */
  watches: 'MODULE_ID_WATCH',
  /** A remote device's frames (SYS_REMOTE.MD). */
  remote: 'MODULE_ID_REMOTE',
  /** The running program's values and heap objects `[op][count]{records}` (VM_CORE.MD "Telemetry is the same command"): the session's. */
  vm: 'MODULE_ID_VM',
} as const

export type StreamName = keyof typeof STREAM_MODULES
export const STREAM_NAMES = Object.keys(STREAM_MODULES) as StreamName[]

export interface LinkDefinitions {
  /** Module numbers by name (`sys`, `session`, `sys_device`, `watch`, ...: `MODULE_ID_<NAME>` in lower case): a request's key is its router's. */
  readonly keys: Readonly<Record<string, number>>
  /** The byte after a request's header: the side of the command it runs (`cmd_dir_e`). */
  readonly dir: { readonly read: number; readonly write: number }
  /** The bits of a frame's type byte (`cmd_type`): the router's flags, an answer's status, the kind. */
  readonly type: { readonly flags: number; readonly status: number; readonly kind: number }
  /** What a frame carries (`cmd_kind_e`). */
  readonly kind: { readonly request: number; readonly answer: number; readonly push: number; readonly control: number }
  /** Who made a frame (`cmd_src`): boards, clients, and the ids no line carries. */
  readonly src: {
    readonly none: number; readonly boardFirst: number; readonly boardLast: number
    readonly clientFirst: number; readonly clientLast: number; readonly self: number; readonly all: number
  }
  /** The session's control frames: their key (`MODULE_ID_SESSION`) and ops (`sys_session_control_e`). */
  readonly control: { readonly key: number; readonly hello: number; readonly ping: number; readonly noSession: number }
  /** Each stream's key (`STREAM_MODULES`). */
  readonly streams: Readonly<Record<StreamName, number>>
  readonly status: { readonly ok: number; readonly error: number }
  readonly flags: { readonly urgent: number; readonly noReply: number }
  /** The runIT service: the session's characteristic (written and notified, framed), and the raw logs and telemetry (notify). */
  readonly gatt: { readonly service: number; readonly session: number; readonly logs: number; readonly telemetry: number }
  readonly frag: { readonly last: number; readonly first: number; readonly countMask: number }
  /** The board's GATT table (BLE LIST) and a BLE provider's create flags. */
  readonly ble: {
    readonly service: { readonly primary: number; readonly locked: number }
    readonly characteristic: { readonly write: number; readonly indicate: number; readonly notify: number; readonly framed: number }
    readonly providerFramed: number
    readonly sendNoWait: number
  }
  /** The error record's header (ERRORS.MD "Error record"). */
  readonly errorRecord: { readonly srcInternal: number; readonly srcCommand: number; readonly srcBoard: number; readonly requestCut: number; readonly stampBytes: number }
  /** `SYS_WATCH_TICK_MS`: the unit of a watch's period and silence. */
  readonly watchTickMs: number
  /** `CMD_FRAME_MAX`: the largest frame, its header included, on every link. */
  readonly frameMax: number
  /** `CMD_HEAD`: the header of every frame (`cmd_hdr_t`); a request's body and an answer's data are at most `frameMax - frameHead`. */
  readonly frameHead: number
}

const value = (enums: EnumTables, group: string, symbol: string): number => {
  const table = enums[group]
  if (!table) throw new Error(`generated/common/enums.json has no group '${group}'.`)
  const member = table.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`Group '${group}' of generated/common/enums.json has no member '${symbol}'.`)
  return member.value
}

export const linkDefinitions = (enums: EnumTables): LinkDefinitions => {
  const v = (group: string, symbol: string) => value(enums, group, symbol)
  const modules = enums.module_id
  if (!modules) throw new Error("generated/common/enums.json has no group 'module_id'.")
  return {
    keys: Object.fromEntries(modules.members.map((m) => [m.name.replace(/^MODULE_ID_/, '').toLowerCase(), m.value])),
    dir: { read: v('cmd_dir_e', 'CMD_DIR_READ'), write: v('cmd_dir_e', 'CMD_DIR_WRITE') },
    type: { flags: v('cmd_type', 'CMD_TYPE_FLAGS'), status: v('cmd_type', 'CMD_TYPE_STATUS'), kind: v('cmd_type', 'CMD_TYPE_KIND') },
    kind: { request: v('cmd_kind_e', 'CMD_REQUEST'), answer: v('cmd_kind_e', 'CMD_ANSWER'), push: v('cmd_kind_e', 'CMD_PUSH'), control: v('cmd_kind_e', 'CMD_CONTROL') },
    src: {
      none: v('cmd_src', 'CMD_SRC_NONE'),
      boardFirst: v('cmd_src', 'CMD_SRC_BOARD_FIRST'),
      boardLast: v('cmd_src', 'CMD_SRC_BOARD_LAST'),
      clientFirst: v('cmd_src', 'CMD_SRC_CLIENT_FIRST'),
      clientLast: v('cmd_src', 'CMD_SRC_CLIENT_LAST'),
      self: v('cmd_src', 'CMD_SRC_SELF'),
      all: v('cmd_src', 'CMD_SRC_ALL'),
    },
    control: {
      key: v('module_id', 'MODULE_ID_SESSION'),
      hello: v('sys_session_control_e', 'SYS_SESSION_HELLO'),
      ping: v('sys_session_control_e', 'SYS_SESSION_PING'),
      noSession: v('sys_session_control_e', 'SYS_SESSION_NO_SESSION'),
    },
    streams: Object.fromEntries(STREAM_NAMES.map((name) => [name, v('module_id', STREAM_MODULES[name])])) as Record<StreamName, number>,
    status: { ok: v('router_status_e', 'ROUTER_STATUS_OK'), error: v('router_status_e', 'ROUTER_STATUS_ERROR') },
    flags: { urgent: v('router_flag', 'ROUTER_FLAG_URGENT'), noReply: v('router_flag', 'ROUTER_FLAG_NOREPLY') },
    gatt: {
      service: v('ble_runit_uuid', 'RUNIT_BLE_SERVICE'),
      session: v('ble_runit_uuid', 'RUNIT_BLE_SESSION'),
      logs: v('ble_runit_uuid', 'RUNIT_BLE_LOGS'),
      telemetry: v('ble_runit_uuid', 'RUNIT_BLE_TELEMETRY'),
    },
    frag: { last: v('ble_frag', 'SYS_BLE_FRAG_LAST'), first: v('ble_frag', 'SYS_BLE_FRAG_FIRST'), countMask: v('ble_frag', 'SYS_BLE_FRAG_COUNT_MASK') },
    ble: {
      service: { primary: v('ble_svc_flag', 'SYS_BLE_SVC_PRIMARY'), locked: v('ble_svc_flag', 'SYS_BLE_SVC_LOCKED') },
      characteristic: {
        write: v('ble_chr_flag', 'SYS_BLE_CHR_WRITE'),
        indicate: v('ble_chr_flag', 'SYS_BLE_CHR_INDICATE'),
        notify: v('ble_chr_flag', 'SYS_BLE_CHR_NOTIFY'),
        framed: v('ble_chr_flag', 'SYS_BLE_CHR_FRAMED'),
      },
      providerFramed: v('provider_ble_flag', 'PROVIDER_BLE_FRAMED'),
      sendNoWait: v('cmd_send_flag', 'CMD_SEND_NOWAIT'),
    },
    errorRecord: {
      srcInternal: v('err_record_src_e', 'ERR_RECORD_SRC_INTERNAL'),
      srcCommand: v('err_record_src_e', 'ERR_RECORD_SRC_COMMAND'),
      srcBoard: v('err_record_src_e', 'ERR_RECORD_SRC_BOARD'),
      requestCut: v('err_record_flag', 'ERR_RECORD_REQ_CUT'),
      stampBytes: v('err_record_flag', 'ERR_RECORD_STAMP_BYTES'),
    },
    watchTickMs: v('watch_tick', 'SYS_WATCH_TICK_MS'),
    frameMax: v('cmd_frame', 'CMD_FRAME_MAX'),
    frameHead: v('cmd_frame', 'CMD_HEAD'),
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
 * The descriptors of the bundle: every system service, device contract, device type and provider type. Vite resolves the globs
 * at build time, so a new `contract_*.json` / `device_*.json` / `provider_*.json` in generated/ is picked up with nothing to edit
 * (the rule of templates/generate_bundle.py).
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
      ...descriptorFiles(import.meta.glob('@generated/providers/provider_*.json', { eager: true, import: 'default' })),
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

/** Every enum group the firmware publishes (`enums.json`). */
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
