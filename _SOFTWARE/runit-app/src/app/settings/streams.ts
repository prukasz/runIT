import { bundledLinkDefinitions, decodeFields, decodeStreamList, requestFrame, serviceCall, STREAM_COMMANDS } from '@backend'
import type { ServiceCall, ServiceCatalog } from '@backend'
import type { DataStream, StreamBinding, StreamRemote } from '../project/projectFile'

/*
 * The data streams (SYS_STREAM.MD): the board's own four (logs, errors, telemetry, events: never created or removed, only bound
 * and unbound) and the project's. A stream is OUT (the board sends it) or IN (what comes in on its bindings), set when it is made.
 * A binding is a provider, by its id; so is an IN stream's output (its way out); a remote target is a provider of another board.
 * The app's own streams are no bindings: it subscribes to them over its session
 * (session/link.ts, Streams). The commands are the backend's `STREAM_COMMANDS` (`contract_stream.json`); LIST's answer is the
 * backend's `decodeStreamList`.
 *
 *   record   the frames of the settings record: the project's streams created, every stream's targets dropped (UNBIND_ALL) and
 *            set (BIND, OUTPUT, REMOTE) (a start replays them over the board's defaults)
 *   apply    what changes the board now: the streams it has and the project not (or with another direction) removed, the new
 *            ones created, then the targets as above, only where they differ
 */

export const STREAMS = STREAM_COMMANDS

/** A stream's direction; a project file from before directions has none: OUT. */
export const dirOf = (s: DataStream): 'out' | 'in' => s.dir ?? 'out'

/** A stream with these targets: none of a kind is no field. */
const withTargets = (s: DataStream, bindings: readonly StreamBinding[], outputs: readonly number[], remotes: readonly StreamRemote[]): DataStream => {
  const { outputs: _outputs, remotes: _remotes, ...rest } = s
  return { ...rest, bindings, ...(outputs.length > 0 ? { outputs } : {}), ...(remotes.length > 0 ? { remotes } : {}) }
}

/** LIST's answer as the project keeps streams: each with its targets (its subscribers are the peers', not the project's). */
export const decodeStreamsInfo = (data: Uint8Array): DataStream[] =>
  decodeStreamList(bundledLinkDefinitions(), data).map((s) => withTargets(
    { stream: s.id, system: s.system, dir: s.dir, bindings: [] },
    s.targets.flatMap((t) => (t.kind === 'provider' && t.provider !== undefined ? [{ provider: t.provider }] : [])),
    s.targets.flatMap((t) => (t.kind === 'output' && t.provider !== undefined ? [t.provider] : [])),
    s.targets.flatMap((t) => (t.kind === 'remote' && t.provider !== undefined && t.peer !== undefined ? [{ board: t.peer, provider: t.provider }] : [])),
  ))

const call = (catalog: ServiceCatalog, command: string, args?: Record<string, number | string>): ServiceCall => serviceCall(catalog, STREAMS.service, command, args)

const same = (a: StreamBinding, b: StreamBinding): boolean => a.provider === b.provider

/** CREATE of a project's stream: its id and direction, no flags. */
const createCall = (catalog: ServiceCatalog, s: DataStream): ServiceCall =>
  call(catalog, STREAMS.create, { id: s.stream, dir: bundledLinkDefinitions().stream.dir[dirOf(s)], flags: 0 })

const sameRemote = (a: StreamRemote, b: StreamRemote): boolean => a.board === b.board && a.provider === b.provider

/**
 * The targets of a stream, set over what the board had (`had`; for the record, over none). UNBIND removes every target of a provider
 * on any board, so a stream that loses one is dropped whole (UNBIND_ALL) and set again.
 */
const bindSteps = (catalog: ServiceCatalog, s: DataStream, had: DataStream | undefined, record: boolean): ServiceCall[] => {
  const [outputs, remotes] = [s.outputs ?? [], s.remotes ?? []]
  const lost = had !== undefined && (had.bindings.some((o) => !s.bindings.some((b) => same(b, o)))
    || (had.outputs ?? []).some((o) => !outputs.includes(o))
    || (had.remotes ?? []).some((o) => !remotes.some((r) => sameRemote(r, o))))
  const drop = record || lost
  const known = drop ? undefined : had
  return [
    ...(drop ? [call(catalog, STREAMS.unbindAll, { id: s.stream })] : []),
    ...s.bindings.filter((b) => !known?.bindings.some((o) => same(o, b))).map((b) => call(catalog, STREAMS.bind, { id: s.stream, provider: b.provider })),
    ...outputs.filter((p) => !known?.outputs?.includes(p)).map((p) => call(catalog, STREAMS.output, { id: s.stream, provider: p })),
    ...remotes.filter((r) => !known?.remotes?.some((o) => sameRemote(o, r))).map((r) => call(catalog, STREAMS.remote, { id: s.stream, board: r.board, provider: r.provider })),
  ]
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
  const [create, bind, unbind, unbindAll, output, remote] = [STREAMS.create, STREAMS.bind, STREAMS.unbind, STREAMS.unbindAll, STREAMS.output, STREAMS.remote].map(op) as [ReturnType<typeof op>, ReturnType<typeof op>, ReturnType<typeof op>, ReturnType<typeof op>, ReturnType<typeof op>, ReturnType<typeof op>]
  const { dir } = bundledLinkDefinitions().stream
  const streams: DataStream[] = [...base]
  for (const frame of frames) {
    if (frame[0] !== entry.key || frame[1] !== catalog.dirs.write) continue // [key][dir][op][args]
    const command = [create, bind, unbind, unbindAll, output, remote].find((c) => c.op === frame[2])
    const layout = command?.write?.args
    if (!command || !layout) continue
    const a = decodeFields(layout, frame.subarray(3)) as Readonly<Record<string, unknown>>
    const stream = Number(a.id)
    const i = streams.findIndex((s) => s.stream === stream)
    if (command === create) {
      const s: DataStream = { stream, system: false, dir: Number(a.dir) === dir.in ? 'in' : 'out', bindings: [] }
      if (i >= 0) streams[i] = s
      else streams.push(s)
      continue
    }
    const s = streams[i]
    if (!s) continue
    const [outputs, remotes] = [s.outputs ?? [], s.remotes ?? []]
    const provider = Number(a.provider)
    if (command === unbindAll) streams[i] = withTargets(s, [], [], [])
    else if (command === unbind) streams[i] = withTargets(s, s.bindings.filter((b) => b.provider !== provider), outputs.filter((p) => p !== provider), remotes.filter((r) => r.provider !== provider))
    else if (command === bind) streams[i] = withTargets(s, [...s.bindings.filter((b) => b.provider !== provider), { provider }], outputs, remotes)
    else if (command === output) streams[i] = withTargets(s, s.bindings, [...outputs.filter((p) => p !== provider), provider], remotes)
    else {
      const r = { board: Number(a.board), provider }
      streams[i] = withTargets(s, s.bindings, outputs, [...remotes.filter((o) => !sameRemote(o, r)), r])
    }
  }
  return streams
}

/** Two lists of streams equal on the board (a stream's name is the project's only). */
export const sameStreams = (a: readonly DataStream[], b: readonly DataStream[]): boolean => {
  const norm = (list: readonly DataStream[]) => JSON.stringify([...list].sort((x, y) => x.stream - y.stream)
    .map((s) => ({ stream: s.stream, system: s.system, dir: dirOf(s), bindings: s.bindings.map((b) => b.provider).sort((x, y) => x - y), outputs: [...(s.outputs ?? [])].sort((x, y) => x - y), remotes: (s.remotes ?? []).map((r) => `${r.board}:${r.provider}`).sort() })))
  return norm(a) === norm(b)
}
