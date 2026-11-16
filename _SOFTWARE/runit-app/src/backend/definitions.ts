import enumsJson from '@generated/common/enums.json'
import errorsJson from '@generated/common/errors.json'
import type { EnumTables } from '@engine/generated/wire'
import { ServiceCatalog } from './catalog/services'
import type { DescriptorFile } from './catalog/services'

/*
 * The one file of the backend that reads generated/. The numbers of the link (generated/common): the router's keys, the
 * direction byte, flags, status, the board's streams (the stream tables of its modules: `log_stream`, `event_stream`) and the
 * session's channels (`session_channel`), the session's frame byte, kinds, control ops and ids (`enums.json`, groups `session_*`,
 * `sys_session_*`), the runIT GATT UUIDs and the fragment header bits of `sys_ble` (`ble_runit_uuid`, `ble_frag`), the largest frame
 * (`cmd_frame`), the errors (`errors.json`: tags, fields, message templates, what an id field refers to) and the enum groups their
 * fields name (`bundledEnums`); and every command of every router (the service, contract and device descriptors,
 * `bundledServiceCatalog`). Nothing here is a number typed in by hand; a symbol the firmware stops publishing stops the load
 * with a message that names it.
 */

export interface LinkDefinitions {
  /** Root keys by name (`sys`, `ble`, `session`, `sys_device`, ...): a router's root key is its module's number (`module_id`), the first key of a request. */
  readonly keys: Readonly<Record<string, number>>
  /** The byte after the root key of every request: the side of the command it runs (`cmd_dir_e`). */
  readonly dir: { readonly read: number; readonly write: number }
  /**
   * The board's streams (DATA_MATRIX.MD "Streams"): the first byte of a PUSH frame's body below the channels. The ones the board's
   * setup routes to a session: `errors` (error records) and `events` (`sys_event_t`s that matter to the user); `logs` is the text
   * of the log characteristic, raw.
   */
  readonly streams: { readonly logs: number; readonly errors: number; readonly events: number }
  /**
   * The session's channels (`session_channel`): a PUSH frame's first byte from `first` on is a module's channel, not a stream: a
   * watch's entries (`watches`), a remote device's frames (`remote`).
   */
  readonly channels: { readonly first: number; readonly watches: number; readonly remote: number }
  readonly status: { readonly ok: number; readonly error: number }
  readonly flags: { readonly urgent: number; readonly noReply: number }
  /** The session (SYS_SESSION.MD "Frames"): the frame byte's bits, the kinds, the control ops, the ids. */
  readonly session: {
    readonly frame: { readonly flags: number; readonly status: number; readonly kind: number }
    readonly kind: { readonly request: number; readonly answer: number; readonly push: number; readonly control: number }
    readonly control: { readonly hello: number; readonly ping: number; readonly noSession: number }
    /** `app`: the id the app says in its hello, the app's own choice (the first of the clients': `clientFirst`); other clients pick one in `clientFirst..clientLast`. */
    readonly id: {
      readonly none: number; readonly self: number; readonly broadcast: number; readonly app: number
      readonly clientFirst: number; readonly clientLast: number; readonly boardLast: number
    }
  }
  /** The runIT service: the characteristic frames come in on (`tx`, the board's notify), the one requests go out on (`rx`, written) and the raw log lines (`logs`). */
  readonly gatt: { readonly service: number; readonly tx: number; readonly rx: number; readonly logs: number }
  readonly frag: { readonly last: number; readonly first: number; readonly countMask: number }
  /** The error record's header (ERRORS.MD "Error record"). */
  readonly errorRecord: { readonly srcInternal: number; readonly srcCommand: number; readonly srcBoard: number; readonly requestCut: number; readonly stampBytes: number }
  /** `CMD_FRAME_MAX` (cmd.h): the largest frame, the one limit of every session's link. */
  readonly frameMax: number
  /** `CMD_HEAD` (cmd.h): the room a frame keeps for its header; a request (keys and payload) is at most `frameMax - frameHead`. */
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
  const modules = enums.module_id
  if (!modules) throw new Error("generated/common/enums.json has no group 'module_id'.")
  const first = value(enums, 'session_channel', 'SYS_SESSION_CHANNEL_FIRST')
  // A channel is its module's number with bit 7 set (`SYS_SESSION_CHANNEL(MODULE_ID_x)`).
  const channel = (module: string): number => first | value(enums, 'module_id', module)
  return {
    keys: Object.fromEntries(modules.members.map((m) => [m.name.replace(/^MODULE_ID_/, '').toLowerCase(), m.value])),
    dir: { read: value(enums, 'cmd_dir_e', 'CMD_DIR_READ'), write: value(enums, 'cmd_dir_e', 'CMD_DIR_WRITE') },
    streams: {
      logs: value(enums, 'log_stream', 'LOG_STREAM_LOGS'),
      errors: value(enums, 'log_stream', 'LOG_STREAM_ERRORS'),
      events: value(enums, 'event_stream', 'EVENT_STREAM_NOTIFY'),
    },
    channels: { first, watches: channel('MODULE_ID_WATCH'), remote: channel('MODULE_ID_REMOTE') },
    status: { ok: value(enums, 'router_status_e', 'ROUTER_STATUS_OK'), error: value(enums, 'router_status_e', 'ROUTER_STATUS_ERROR') },
    flags: { urgent: value(enums, 'router_flag', 'ROUTER_FLAG_URGENT'), noReply: value(enums, 'router_flag', 'ROUTER_FLAG_NOREPLY') },
    session: {
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
      },
      id: {
        none: value(enums, 'session_id', 'SYS_SESSION_ID_NONE'),
        self: value(enums, 'session_id', 'SYS_SESSION_ID_SELF'),
        broadcast: value(enums, 'session_id', 'SYS_SESSION_ID_BROADCAST'),
        app: value(enums, 'session_id', 'SYS_SESSION_ID_CLIENT_FIRST'),
        clientFirst: value(enums, 'session_id', 'SYS_SESSION_ID_CLIENT_FIRST'),
        clientLast: value(enums, 'session_id', 'SYS_SESSION_ID_CLIENT_LAST'),
        boardLast: value(enums, 'session_id', 'SYS_SESSION_ID_LAST'),
      },
    },
    gatt: {
      service: value(enums, 'ble_runit_uuid', 'RUNIT_BLE_SERVICE'),
      tx: value(enums, 'ble_runit_uuid', 'RUNIT_BLE_TX'),
      rx: value(enums, 'ble_runit_uuid', 'RUNIT_BLE_RX'),
      logs: value(enums, 'ble_runit_uuid', 'RUNIT_BLE_LOGS'),
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
    frameMax: value(enums, 'cmd_frame', 'CMD_FRAME_MAX'),
    frameHead: value(enums, 'cmd_frame', 'CMD_HEAD'),
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
