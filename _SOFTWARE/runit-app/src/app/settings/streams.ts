import { bundledEnums, decodeFields, requestFrame, serviceCall } from '@backend'
import type { ServiceCall, ServiceCatalog } from '@backend'
import type { DataStream, StreamBinding } from '../project/projectFile'

/*
 * The data streams (SYS_CONNECTOR.MD): the board's own four (logs, errors, telemetry, events: never created or removed, only
 * bound, unbound and paused) and the project's. The commands and fields are the connector descriptor's (`contract_connector.json`),
 * the transports `transport_e` (enums.json); this file knows which command does what and the layout of INFO's answer.
 *
 *   record   the frames of the settings record: the project's streams created, every stream's bindings set (BIND), the
 *            transports it is not sent on unbound, the paused ones suspended (a start replays them over the board's defaults)
 *   apply    what changes the board now: the streams it has and the project not (or changed) removed, the new ones created,
 *            then the bindings and pauses as above, only where they differ
 */

export const STREAMS = {
  service: 'CONNECTOR', create: 'CREATE', remove: 'REMOVE', bind: 'BIND', unbind: 'UNBIND', suspend: 'SUSPEND', resume: 'RESUME', info: 'INFO',
} as const

/** The transports a stream can be sent on (`transport_e`): their values and names. */
export const transports = (): { readonly value: number; readonly label: string }[] =>
  (bundledEnums().transport_e?.members ?? []).map((m) => ({ value: m.value, label: m.alias ?? m.name }))

const u32 = (d: Uint8Array, at: number): number => (d[at]! | (d[at + 1]! << 8) | (d[at + 2]! << 16)) + d[at + 3]! * 0x1000000

/** INFO's answer: `u8 count`, per stream `u8 connector, u8 flags (system, suspended), u8 n, n × {u8 provider, u32 endpoint}`. */
export const decodeStreamsInfo = (data: Uint8Array): DataStream[] => {
  const out: DataStream[] = []
  let at = 1
  for (let n = 0; n < (data[0] ?? 0) && at + 3 <= data.length; n++) {
    const [stream, flags, count] = [data[at]!, data[at + 1]!, data[at + 2]!]
    at += 3
    const bindings: StreamBinding[] = []
    for (let b = 0; b < count; b++, at += 5) bindings.push({ provider: data[at]!, endpoint: u32(data, at + 1) })
    out.push({ stream, system: (flags & 1) !== 0, suspended: (flags & 2) !== 0, bindings })
  }
  return out
}

const call = (catalog: ServiceCatalog, command: string, args?: Record<string, number | string>): ServiceCall => serviceCall(catalog, STREAMS.service, command, args)

/** The bindings and the pause of a stream, set over what the board had (`had`; for the record, the board's defaults are unknown: every transport not wanted is unbound). */
const bindSteps = (catalog: ServiceCatalog, s: DataStream, had: DataStream | undefined, record: boolean): ServiceCall[] => {
  const steps: ServiceCall[] = []
  for (const b of s.bindings) {
    const old = had?.bindings.find((o) => o.provider === b.provider)
    if (record || !old || old.endpoint !== b.endpoint) steps.push(call(catalog, STREAMS.bind, { connector: s.stream, provider: b.provider, endpoint: b.endpoint }))
  }
  // Unbound after the binds, so a board stream always keeps one transport (the board refuses to drop its last).
  const unbind = record ? transports().map((t) => t.value) : (had?.bindings ?? []).map((b) => b.provider)
  for (const provider of unbind) if (!s.bindings.some((b) => b.provider === provider)) steps.push(call(catalog, STREAMS.unbind, { connector: s.stream, provider }))
  if (record ? s.suspended : (had?.suspended ?? false) !== s.suspended) steps.push(call(catalog, s.suspended ? STREAMS.suspend : STREAMS.resume, { connector: s.stream }))
  return steps
}

/** The frames of the streams record. None when the project keeps no streams. */
export const streamsRecord = (catalog: ServiceCatalog, streams: readonly DataStream[]): Uint8Array[] =>
  streams.flatMap((s) => [
    ...(s.system ? [] : [call(catalog, STREAMS.create, { connector: s.stream })]),
    ...bindSteps(catalog, s, undefined, true),
  ]).map((c) => requestFrame(c.request))

/** The commands that bring the board's streams to the project's. */
export const streamsApply = (catalog: ServiceCatalog, live: readonly DataStream[], streams: readonly DataStream[]): ServiceCall[] => {
  const steps: ServiceCall[] = []
  for (const old of live.filter((s) => !s.system)) {
    if (!streams.some((s) => s.stream === old.stream)) steps.push(call(catalog, STREAMS.remove, { connector: old.stream }))
  }
  for (const s of streams) {
    const old = live.find((o) => o.stream === s.stream)
    if (!s.system && !old) steps.push(call(catalog, STREAMS.create, { connector: s.stream }))
    steps.push(...bindSteps(catalog, s, old, false))
  }
  return steps
}

/** The streams a stored record leaves, replayed over the board's streams (`base`, its INFO). */
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
    const stream = Number(a.connector)
    const i = streams.findIndex((s) => s.stream === stream)
    if (command === create) {
      const s: DataStream = { stream, system: false, suspended: false, bindings: [] }
      if (i >= 0) streams[i] = s
      else streams.push(s)
    } else {
      const s = streams[i]
      if (!s) continue
      if (command === bind) streams[i] = { ...s, bindings: [...s.bindings.filter((b) => b.provider !== Number(a.provider)), { provider: Number(a.provider), endpoint: Number(a.endpoint) }] }
      else if (command === unbind) streams[i] = { ...s, bindings: s.bindings.filter((b) => b.provider !== Number(a.provider)) }
      else streams[i] = { ...s, suspended: command === suspend }
    }
  }
  return streams
}

/** Two lists of streams equal on the board (a stream's name is the project's only). */
export const sameStreams = (a: readonly DataStream[], b: readonly DataStream[]): boolean => {
  const norm = (list: readonly DataStream[]) => JSON.stringify([...list].sort((x, y) => x.stream - y.stream)
    .map((s) => ({ stream: s.stream, system: s.system, suspended: s.suspended, bindings: [...s.bindings].sort((x, y) => x.provider - y.provider).map((b) => ({ provider: b.provider, endpoint: b.endpoint })) })))
  return norm(a) === norm(b)
}
