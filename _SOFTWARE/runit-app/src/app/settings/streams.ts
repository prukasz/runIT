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

/** INFO's answer: `u8 count`, per stream `u8 id, u8 stream, u8 flags (system, suspended), u8 n, n × {u8 provider, u32 endpoint}, name\0`. */
export const decodeStreamsInfo = (data: Uint8Array): DataStream[] => {
  const out: DataStream[] = []
  let at = 1
  for (let n = 0; n < (data[0] ?? 0) && at + 4 <= data.length; n++) {
    const [id, stream, flags, count] = [data[at]!, data[at + 1]!, data[at + 2]!, data[at + 3]!]
    at += 4
    const bindings: StreamBinding[] = []
    for (let b = 0; b < count; b++, at += 5) bindings.push({ provider: data[at]!, endpoint: u32(data, at + 1) })
    let end = at
    while (end < data.length && data[end] !== 0) end++
    const name = new TextDecoder().decode(data.subarray(at, end))
    at = end + 1
    out.push({ id, stream, name, system: (flags & 1) !== 0, suspended: (flags & 2) !== 0, bindings })
  }
  return out
}

const call = (catalog: ServiceCatalog, command: string, args?: Record<string, number | string>): ServiceCall => serviceCall(catalog, STREAMS.service, command, args)

/** The bindings and the pause of a stream, set over what the board had (`had`; for the record, the board's defaults are unknown: every transport not wanted is unbound). */
const bindSteps = (catalog: ServiceCatalog, s: DataStream, had: DataStream | undefined, record: boolean): ServiceCall[] => {
  const steps: ServiceCall[] = []
  for (const b of s.bindings) {
    const old = had?.bindings.find((o) => o.provider === b.provider)
    if (record || !old || old.endpoint !== b.endpoint) steps.push(call(catalog, STREAMS.bind, { id: s.id, provider: b.provider, endpoint: b.endpoint }))
  }
  // Unbound after the binds, so a board stream always keeps one transport (the board refuses to drop its last).
  const unbind = record ? transports().map((t) => t.value) : (had?.bindings ?? []).map((b) => b.provider)
  for (const provider of unbind) if (!s.bindings.some((b) => b.provider === provider)) steps.push(call(catalog, STREAMS.unbind, { id: s.id, provider }))
  if (record ? s.suspended : (had?.suspended ?? false) !== s.suspended) steps.push(call(catalog, s.suspended ? STREAMS.suspend : STREAMS.resume, { id: s.id }))
  return steps
}

/** The frames of the streams record. None when the project keeps no streams. */
export const streamsRecord = (catalog: ServiceCatalog, streams: readonly DataStream[]): Uint8Array[] =>
  streams.flatMap((s) => [
    ...(s.system ? [] : [call(catalog, STREAMS.create, { id: s.id, stream: s.stream, name: s.name })]),
    ...bindSteps(catalog, s, undefined, true),
  ]).map((c) => requestFrame(c.request))

const sameIdentity = (a: DataStream, b: DataStream): boolean => a.stream === b.stream && a.name === b.name

/** The commands that bring the board's streams to the project's. */
export const streamsApply = (catalog: ServiceCatalog, live: readonly DataStream[], streams: readonly DataStream[]): ServiceCall[] => {
  const steps: ServiceCall[] = []
  for (const old of live.filter((s) => !s.system)) {
    const next = streams.find((s) => s.id === old.id)
    if (!next || !sameIdentity(next, old)) steps.push(call(catalog, STREAMS.remove, { id: old.id }))
  }
  for (const s of streams) {
    const old = live.find((o) => o.id === s.id)
    const fresh = !s.system && (!old || !sameIdentity(s, old))
    if (fresh) steps.push(call(catalog, STREAMS.create, { id: s.id, stream: s.stream, name: s.name }))
    steps.push(...bindSteps(catalog, s, fresh ? undefined : old, false))
  }
  return steps
}

/** The streams a stored record leaves, replayed over the board's streams (`base`, its INFO). */
export const streamsFromRecord = (catalog: ServiceCatalog, frames: readonly Uint8Array[], base: readonly DataStream[]): DataStream[] => {
  const entry = catalog.service(STREAMS.service)
  const op = (id: string) => catalog.command(entry, id)
  const [create, bind, unbind, suspend, resume] = [op(STREAMS.create), op(STREAMS.bind), op(STREAMS.unbind), op(STREAMS.suspend), op(STREAMS.resume)]
  const streams: DataStream[] = [...base]
  const find = (id: number) => streams.find((s) => s.id === id)
  for (const frame of frames) {
    if (frame[0] !== entry.key) continue
    const command = [create, bind, unbind, suspend, resume].find((c) => c.op === frame[1])
    if (!command?.args) continue
    const a = decodeFields(command.args, frame.subarray(2)) as Readonly<Record<string, unknown>>
    const id = Number(a.id)
    if (command === create) {
      const at = streams.findIndex((s) => s.id === id)
      const s: DataStream = { id, stream: Number(a.stream), name: String(a.name ?? ''), system: false, suspended: false, bindings: [] }
      if (at >= 0) streams[at] = s
      else streams.push(s)
    } else {
      const s = find(id)
      if (!s) continue
      const i = streams.indexOf(s)
      if (command === bind) streams[i] = { ...s, bindings: [...s.bindings.filter((b) => b.provider !== Number(a.provider)), { provider: Number(a.provider), endpoint: Number(a.endpoint) }] }
      else if (command === unbind) streams[i] = { ...s, bindings: s.bindings.filter((b) => b.provider !== Number(a.provider)) }
      else streams[i] = { ...s, suspended: command === suspend }
    }
  }
  return streams
}

/** Two lists of streams equal. */
export const sameStreams = (a: readonly DataStream[], b: readonly DataStream[]): boolean => {
  const norm = (list: readonly DataStream[]) => JSON.stringify([...list].sort((x, y) => x.id - y.id).map((s) => ({ ...s, bindings: [...s.bindings].sort((x, y) => x.provider - y.provider) })))
  return norm(a) === norm(b)
}
