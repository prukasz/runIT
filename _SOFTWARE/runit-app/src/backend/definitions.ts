import enumsJson from '@generated/common/enums.json'
import errorsJson from '@generated/common/errors.json'
import limitsJson from '@generated/common/limits.json'
import type { EnumTables } from '@engine/generated/wire'

/*
 * The numbers of the link, read from what the firmware publishes (generated/common): the router's keys, flags, status and
 * the stream bytes (`enums.json`), the runIT GATT UUIDs and the fragment header bits of `sys_ble` (`enums.json`, groups
 * `ble_runit_uuid` and `ble_frag`), the largest frame (`limits.json`) and the error tags (`errors.json`). Nothing here is a
 * number typed in by hand; a symbol the firmware stops publishing stops the load with a message that names it.
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
  readonly errorRecord: { readonly srcInternal: number; readonly srcCommand: number; readonly requestCut: number }
  /** `CONFIG_ROUTER_FRAME_MAX`: the largest request (and response) frame. A link may carry less. */
  readonly frameMax: number
}

const value = (enums: EnumTables, group: string, symbol: string): number => {
  const table = enums[group]
  if (!table) throw new Error(`generated/common/enums.json has no group '${group}'.`)
  const member = table.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`Group '${group}' of generated/common/enums.json has no member '${symbol}'.`)
  return member.value
}

export const linkDefinitions = (enums: EnumTables, frameMax: number): LinkDefinitions => {
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
    },
    frameMax,
  }
}

/** The definitions of the bundled firmware descriptors. */
export const bundledLinkDefinitions = (): LinkDefinitions => {
  const limit = (limitsJson as unknown as { limits: Record<string, { value: number }> }).limits.ROUTER_FRAME_MAX
  if (!limit) throw new Error("generated/common/limits.json has no limit 'ROUTER_FRAME_MAX'.")
  return linkDefinitions((enumsJson as unknown as { enums: EnumTables }).enums, limit.value)
}

/* ---- error tags ---- */

export interface ErrorField {
  readonly name: string
  readonly type: string
  readonly offset: number
  readonly size: number
}

export interface ErrorInfo {
  readonly tag: number
  readonly name: string
  readonly module: string
  readonly severity: number
  readonly description: string
  readonly fields: readonly ErrorField[]
}

interface ErrorsFile {
  readonly modules: readonly {
    readonly name: string
    readonly tags: readonly { readonly tag: number; readonly name: string; readonly severity: number; readonly description?: string; readonly fields: readonly ErrorField[] }[]
  }[]
}

/** Every error the firmware can raise, by tag (`errors.json`). */
export const errorCatalog = (file: ErrorsFile = errorsJson as unknown as ErrorsFile): ReadonlyMap<number, ErrorInfo> =>
  new Map(file.modules.flatMap((m) => m.tags.map((t): [number, ErrorInfo] => [t.tag, { tag: t.tag, name: t.name, module: m.name, severity: t.severity, description: t.description ?? '', fields: t.fields }])))
