import { bundledEnums, bundledLinkDefinitions, decodeFields, decodeStreamList, requestFrame, serviceCall, STREAM_COMMANDS } from '@backend'
import type { ServiceCall, ServiceCatalog } from '@backend'
import type { DataStream, StreamBinding } from '../project/projectFile'

/*
 * The data streams (SYS_STREAM.MD): the board's own four (logs, errors, telemetry, events: never created or removed, only bound
 * and unbound) and the project's. A stream is OUT (the board sends it) or IN (what comes in on its bindings), set when it is made.
 * A binding is a provider: its link (`provider`, `sys_data_provider_link_e`) and its channel there (`endpoint`: a BLE
 * characteristic's UUID, a UART port). The app's own streams are no bindings: it subscribes to them over its session
 * (session/link.ts, Streams). The commands are the backend's `STREAM_COMMANDS` (`contract_stream.json`); LIST's answer is the
 * backend's `decodeStreamList`.
 *
 *   record   the frames of the settings record: the project's streams created, every stream's bindings dropped (UNBIND of
 *            link 0) and set (BIND) (a start replays them over the board's defaults)
 *   apply    what changes the board now: the streams it has and the project not (or with another direction) removed, the new
 *            ones created, then the bindings as above, only where they differ
 */

export const STREAMS = STREAM_COMMANDS

/** The links a stream can be bound to (`sys_data_provider_link_e`): their values and names. */
export const transports = (): { readonly value: number; readonly label: string }[] =>
  (bundledEnums().sys_data_provider_link_e?.members ?? []).map((m) => ({ value: m.value, label: m.alias ?? m.name }))

/** A stream's direction; a project file from before directions has none: OUT. */
export const dirOf = (s: DataStream): 'out' | 'in' => s.dir ?? 'out'

/** LIST's answer as the project keeps streams: each with its bindings (its subscribers are the peers', not the project's). */
export const decodeStreamsInfo = (data: Uint8Array): DataStream[] =>
  decodeStreamList(bundledLinkDefinitions(), data).map((s) => ({
    stream: s.id,
    system: s.system,
    dir: s.dir,
    bindings: s.targets.flatMap((t) => (t.kind === 'provider' && t.provider ? [{ provider: t.provider.link, endpoint: t.provider.channel }] : [])),
  }))

const call = (catalog: ServiceCatalog, command: string, args?: Record<string, number | string>): ServiceCall => serviceCall(catalog, STREAMS.service, command, args)

const same = (a: StreamBinding, b: StreamBinding): boolean => a.provider === b.provider && a.endpoint === b.endpoint

/** CREATE of a project's stream: its id and direction, no flags. */
const createCall = (catalog: ServiceCatalog, s: DataStream): ServiceCall =>
  call(catalog, STREAMS.create, { id: s.stream, dir: bundledLinkDefinitions().stream.dir[dirOf(s)], flags: 0 })

/** UNBIND of link 0: every binding of the stream (SYS_STREAM.MD "Remote"). */
const UNBIND_ALL = { link: 0, channel: 0 }

/** The bindings of a stream, set over what the board had (`had`; for the record, over none: every binding is dropped first). */
const bindSteps = (catalog: ServiceCatalog, s: DataStream, had: DataStream | undefined, record: boolean): ServiceCall[] => {
  const steps: ServiceCall[] = record ? [call(catalog, STREAMS.unbind, { id: s.stream, ...UNBIND_ALL })] : []
  for (const b of s.bindings) {
    if (record || !had?.bindings.some((o) => same(o, b))) steps.push(call(catalog, STREAMS.bind, { id: s.stream, link: b.provider, channel: b.endpoint }))
  }
  for (const old of had?.bindings ?? []) if (!s.bindings.some((b) => same(b, old))) steps.push(call(catalog, STREAMS.unbind, { id: s.stream, link: old.provider, channel: old.endpoint }))
  return steps
}

/** The frames of the streams record. None when the project keeps no streams. */
export const streamsRecord = (catalog: ServiceCatalog, streams: readonly DataStream[]): Uint8Array[] =>
  streams.flatMap((s) => [...(s.system ? [] : [createCall(catalog, s)]), ...bindSteps(catalog, s, undefined, true)]).map((c) => requestFrame(c.request))

/** The commands that bring the board's streams to the project's. */
export const streamsApply = (catalog: ServiceCatalog, live: readonly DataStream[], streams: readonly DataStream[]): ServiceCall[] => {
  const steps: ServiceCall[] = []
  // A stream's direction is set when it is made: one the project has the other way is made again.
  const remade = (old: DataStream) => streams.some((s) => s.stream === old.stream && !s.system && dirOf(s) !== dirOf(old))
  for (const old of live.filter((s) => !s.system)) {
    if (!streams.some((s) => s.stream === old.stream) || remade(old)) steps.push(call(catalog, STREAMS.remove, { id: old.stream }))
  }
  for (const s of streams) {
    const found = live.find((o) => o.stream === s.stream)
    const old = found && !remade(found) ? found : undefined
    if (!s.system && !old) steps.push(createCall(catalog, s))
    steps.push(...bindSteps(catalog, s, old, false))
  }
  return steps
}

/** The streams a stored record leaves, replayed over the board's streams (`base`, its LIST). */
export const streamsFromRecord = (catalog: ServiceCatalog, frames: readonly Uint8Array[], base: readonly DataStream[]): DataStream[] => {
  const entry = catalog.service(STREAMS.service)
  const op = (id: string) => catalog.command(entry, id)
  const [create, bind, unbind] = [op(STREAMS.create), op(STREAMS.bind), op(STREAMS.unbind)]
  const { dir } = bundledLinkDefinitions().stream
  const streams: DataStream[] = [...base]
  for (const frame of frames) {
    if (frame[0] !== entry.key || frame[1] !== catalog.dirs.write) continue // [key][dir][op][args]
    const command = [create, bind, unbind].find((c) => c.op === frame[2])
    const layout = command?.write?.args
    if (!command || !layout) continue
    const a = decodeFields(layout, frame.subarray(3)) as Readonly<Record<string, unknown>>
    const stream = Number(a.id)
    const i = streams.findIndex((s) => s.stream === stream)
    if (command === create) {
      const s: DataStream = { stream, system: false, dir: Number(a.dir) === dir.in ? 'in' : 'out', bindings: [] }
      if (i >= 0) streams[i] = s
      else streams.push(s)
    } else {
      const s = streams[i]
      if (!s) continue
      const binding = { provider: Number(a.link), endpoint: Number(a.channel) }
      if (command === bind) streams[i] = { ...s, bindings: [...s.bindings.filter((b) => !same(b, binding)), binding] }
      else streams[i] = { ...s, bindings: binding.provider === UNBIND_ALL.link ? [] : s.bindings.filter((b) => !same(b, binding)) }
    }
  }
  return streams
}

/** Two lists of streams equal on the board (a stream's name is the project's only). */
export const sameStreams = (a: readonly DataStream[], b: readonly DataStream[]): boolean => {
  const norm = (list: readonly DataStream[]) => JSON.stringify([...list].sort((x, y) => x.stream - y.stream)
    .map((s) => ({ stream: s.stream, system: s.system, dir: dirOf(s), bindings: [...s.bindings].sort((x, y) => x.provider - y.provider || x.endpoint - y.endpoint).map((b) => ({ provider: b.provider, endpoint: b.endpoint })) })))
  return norm(a) === norm(b)
}
