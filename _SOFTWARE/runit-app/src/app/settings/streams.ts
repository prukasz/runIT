import { bundledEnums, decodeFields, requestFrame, serviceCall } from '@backend'
import type { ServiceCall, ServiceCatalog } from '@backend'
import type { DataStream, StreamBinding } from '../project/projectFile'

/*
 * The data streams (SYS_DATA_CONNECTOR.MD): the board's own four (logs, errors, telemetry, events: never created or removed, only
 * bound, unbound and paused) and the project's. A binding is a provider: its link (`provider`, `sys_data_provider_link_e`) and
 * its channel there (`endpoint`: a BLE characteristic's UUID, a UART port). The app's own streams are no bindings: it subscribes
 * to them over its session (session/link.ts). The commands and fields are the connector descriptor's
 * (`contract_connector.json`); this file knows which command does what and the layout of LIST's answer.
 *
 *   record   the frames of the settings record: the project's streams created, every stream's bindings dropped (UNBIND of
 *            link 0) and set (BIND), the paused ones suspended (a start replays them over the board's defaults)
 *   apply    what changes the board now: the streams it has and the project not (or changed) removed, the new ones created,
 *            then the bindings and pauses as above, only where they differ
 */

export const STREAMS = {
  service: 'CONNECTOR', create: 'CREATE', remove: 'REMOVE', bind: 'BIND', unbind: 'UNBIND', suspend: 'SUSPEND', resume: 'RESUME', info: 'LIST',
} as const

/** The links a stream can be bound to (`sys_data_provider_link_e`): their values and names. */
export const transports = (): { readonly value: number; readonly label: string }[] =>
  (bundledEnums().sys_data_provider_link_e?.members ?? []).map((m) => ({ value: m.value, label: m.alias ?? m.name }))

/** A binding's direction: it sends (`SYS_DATA_CONNECTOR_BIND_TX`). */
const bindTx = (): number => bundledEnums().conn_bind?.members.find((m) => m.name === 'SYS_DATA_CONNECTOR_BIND_TX')?.value ?? 2

const u32 = (d: Uint8Array, at: number): number => (d[at]! | (d[at + 1]! << 8) | (d[at + 2]! << 16)) + d[at + 3]! * 0x1000000

const HEAD_BYTES = 5 // id, flags, state, subscribers, bindings
const SUBSCRIBER_BYTES = 3 // peer, u16 dropped
const BINDING_BYTES = 8 // link, u32 channel, flags, u16 dropped

/** LIST's answer: `u8 count`, per stream its head, its subscribers, its bindings (SYS_DATA_CONNECTOR.MD "Remote"). */
export const decodeStreamsInfo = (data: Uint8Array): DataStream[] => {
  const out: DataStream[] = []
  let at = 1
  for (let n = 0; n < (data[0] ?? 0) && at + HEAD_BYTES <= data.length; n++) {
    const [stream, , state, subscribers, count] = [data[at]!, data[at + 1]!, data[at + 2]!, data[at + 3]!, data[at + 4]!]
    at += HEAD_BYTES + subscribers * SUBSCRIBER_BYTES
    const bindings: StreamBinding[] = []
    for (let b = 0; b < count; b++, at += BINDING_BYTES) bindings.push({ provider: data[at]!, endpoint: u32(data, at + 1) })
    out.push({ stream, system: (state & 1) !== 0, suspended: (state & 2) !== 0, bindings })
  }
  return out
}

const call = (catalog: ServiceCatalog, command: string, args?: Record<string, number | string>): ServiceCall => serviceCall(catalog, STREAMS.service, command, args)

const same = (a: StreamBinding, b: StreamBinding): boolean => a.provider === b.provider && a.endpoint === b.endpoint

/** UNBIND of link 0: every binding of the stream (SYS_DATA_CONNECTOR.MD "Remote"). */
const UNBIND_ALL = { link: 0, channel: 0 }

/** The bindings and the pause of a stream, set over what the board had (`had`; for the record, over none: every binding is dropped first). */
const bindSteps = (catalog: ServiceCatalog, s: DataStream, had: DataStream | undefined, record: boolean): ServiceCall[] => {
  const steps: ServiceCall[] = record ? [call(catalog, STREAMS.unbind, { id: s.stream, ...UNBIND_ALL })] : []
  for (const b of s.bindings) {
    if (record || !had?.bindings.some((o) => same(o, b))) steps.push(call(catalog, STREAMS.bind, { id: s.stream, link: b.provider, channel: b.endpoint, flags: bindTx() }))
  }
  for (const old of had?.bindings ?? []) if (!s.bindings.some((b) => same(b, old))) steps.push(call(catalog, STREAMS.unbind, { id: s.stream, link: old.provider, channel: old.endpoint }))
  if (record ? s.suspended : (had?.suspended ?? false) !== s.suspended) steps.push(call(catalog, s.suspended ? STREAMS.suspend : STREAMS.resume, { id: s.stream }))
  return steps
}

/** The frames of the streams record. None when the project keeps no streams. */
export const streamsRecord = (catalog: ServiceCatalog, streams: readonly DataStream[]): Uint8Array[] =>
  streams.flatMap((s) => [
    ...(s.system ? [] : [call(catalog, STREAMS.create, { id: s.stream, flags: 0 })]),
    ...bindSteps(catalog, s, undefined, true),
  ]).map((c) => requestFrame(c.request))

/** The commands that bring the board's streams to the project's. */
export const streamsApply = (catalog: ServiceCatalog, live: readonly DataStream[], streams: readonly DataStream[]): ServiceCall[] => {
  const steps: ServiceCall[] = []
  for (const old of live.filter((s) => !s.system)) {
    if (!streams.some((s) => s.stream === old.stream)) steps.push(call(catalog, STREAMS.remove, { id: old.stream }))
  }
  for (const s of streams) {
    const old = live.find((o) => o.stream === s.stream)
    if (!s.system && !old) steps.push(call(catalog, STREAMS.create, { id: s.stream, flags: 0 }))
    steps.push(...bindSteps(catalog, s, old, false))
  }
  return steps
}

/** The streams a stored record leaves, replayed over the board's streams (`base`, its LIST). */
export const streamsFromRecord = (catalog: ServiceCatalog, frames: readonly Uint8Array[], base: readonly DataStream[]): DataStream[] => {
  const entry = catalog.service(STREAMS.service)
  const op = (id: string) => catalog.command(entry, id)
  const [create, bind, unbind, suspend, resume] = [op(STREAMS.create), op(STREAMS.bind), op(STREAMS.unbind), op(STREAMS.suspend), op(STREAMS.resume)]
  const streams: DataStream[] = [...base]
  for (const frame of frames) {
    if (frame[0] !== entry.key || frame[1] !== catalog.dirs.write) continue // [key][dir][op][args]
    const command = [create, bind, unbind, suspend, resume].find((c) => c.op === frame[2])
    const layout = command?.write?.args
    if (!command || !layout) continue
    const a = decodeFields(layout, frame.subarray(3)) as Readonly<Record<string, unknown>>
    const stream = Number(a.id)
    const i = streams.findIndex((s) => s.stream === stream)
    if (command === create) {
      const s: DataStream = { stream, system: false, suspended: false, bindings: [] }
      if (i >= 0) streams[i] = s
      else streams.push(s)
    } else {
      const s = streams[i]
      if (!s) continue
      const binding = { provider: Number(a.link), endpoint: Number(a.channel) }
      if (command === bind) streams[i] = { ...s, bindings: [...s.bindings.filter((b) => !same(b, binding)), binding] }
      else if (command === unbind) streams[i] = { ...s, bindings: binding.provider === UNBIND_ALL.link ? [] : s.bindings.filter((b) => !same(b, binding)) }
      else streams[i] = { ...s, suspended: command === suspend }
    }
  }
  return streams
}

/** Two lists of streams equal on the board (a stream's name is the project's only). */
export const sameStreams = (a: readonly DataStream[], b: readonly DataStream[]): boolean => {
  const norm = (list: readonly DataStream[]) => JSON.stringify([...list].sort((x, y) => x.stream - y.stream)
    .map((s) => ({ stream: s.stream, system: s.system, suspended: s.suspended, bindings: [...s.bindings].sort((x, y) => x.provider - y.provider || x.endpoint - y.endpoint).map((b) => ({ provider: b.provider, endpoint: b.endpoint })) })))
  return norm(a) === norm(b)
}
