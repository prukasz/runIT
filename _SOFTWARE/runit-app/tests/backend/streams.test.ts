import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, decodePeers, decodeStreamList, RunitLink, SESSION_COMMANDS, Sessions, STREAM_COMMANDS, Streams } from '@backend'
import type { LinkDiagnostic, StreamFrame } from '@backend'
import { answer, BOARD_MAC, CLIENT_ID, defs, FakeBoard, until } from './fakes'

/*
 * The session as the firmware has it (SYS_SESSION.MD): each side says its own id in its hello, the streams are the STREAM
 * router's (SYS_STREAM.MD), a STREAM frame from 0x80 is a session channel. And the two modules over them: Streams, Sessions.
 */

const catalog = bundledServiceCatalog()
const op = (service: string, command: string) => catalog.command(catalog.service(service), command).op
const u32 = (n: number) => [n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff, (n >>> 24) & 0xff]

const connected = async (setup?: (board: FakeBoard) => void) => {
  const board = new FakeBoard()
  board.respond = (frame, send) => send(answer(frame[0]!))
  setup?.(board)
  const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
  await link.connect(board.device)
  return { board, link }
}

describe('the session', () => {
  it('says the app\'s own id in its hello, takes the board\'s id from the board\'s hello and sends to it', async () => {
    const { board, link } = await connected((b) => (b.boardId = 3))
    expect(board.clientHello).toMatchObject({ version: defs.session.version, id: defs.session.id.app, mac: '00:00:00:00:00:00', timeoutMs: 60000 })
    expect(link.session).toMatchObject({ clientId: CLIENT_ID, boardId: 3, mac: BOARD_MAC.map((b) => b.toString(16)).join(':'), bootId: 1, fwVersion: 7, timeoutMs: 3000 })
    await link.request({ keys: [defs.keys.sys!, defs.dir.read, 0x01] })
    expect(board.addresses.at(-1)).toEqual({ dst: 3, src: CLIENT_ID })
  })

  it('subscribes the errors, telemetry and events streams with STREAM SUBSCRIBE when it opens', async () => {
    const { board } = await connected()
    expect(board.subscribed).toEqual([defs.streams.errors, defs.streams.telemetry, defs.streams.events])
  })

  it('a client that picks its own id says that one', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: false, clientId: defs.session.id.clientFirst })
    await link.connect(board.device)
    expect(board.clientHello?.id).toBe(defs.session.id.clientFirst)
    expect(link.session?.clientId).toBe(defs.session.id.clientFirst)
  })

  it('hands a session channel\'s frames over by name: a watch\'s entries, a remote device\'s events and errors', async () => {
    const { board, link } = await connected()
    const got: StreamFrame[] = []
    link.onStream((f) => got.push(f))
    board.stream(defs.channels.watches, [CLIENT_ID, 1, 0, 9], 4)
    board.stream(defs.channels.remote, [0x05, 0x00, 0, 0xaa], 2)
    board.stream(defs.streams.errors, [0x01])
    await until(() => got.length === 3)
    expect(got.map((f) => [f.name, f.stream, f.seq])).toEqual([['watches', 0x80, 4], ['remote', 0x81, 2], ['errors', defs.streams.errors, 0]])
    expect([...got[1]!.data]).toEqual([0x05, 0x00, 0, 0xaa])
  })

  it('a user stream is no unknown stream; a byte nobody publishes is', async () => {
    const { board, link } = await connected()
    const diagnostics: LinkDiagnostic[] = []
    link.onDiagnostic((d) => diagnostics.push(d))
    const frames: StreamFrame[] = []
    link.onStream((f) => frames.push(f))
    board.stream(0x10, [1])
    board.stream(0x05, [1]) // below the user streams, not one of the board's own
    board.stream(0x9f, [1]) // a channel nobody assigned
    await until(() => frames.length === 3)
    expect(frames[0]!.stream).toBe(0x10)
    expect(frames[0]!.name).toBeUndefined()
    expect(diagnostics).toEqual([{ kind: 'unknown-stream', stream: 0x05 }, { kind: 'unknown-stream', stream: 0x9f }])
  })
})

describe('STREAM LIST and SESSION PEERS, as the firmware packs them', () => {
  it('reads every stream with its targets: a subscriber, a binding, the board\'s code', () => {
    const list = Uint8Array.of(
      2,
      0x04, defs.stream.dir.out, defs.stream.flagUrgent, defs.stream.stateSystem, 2, // errors: OUT, urgent, the board's, 2 targets
      defs.stream.target.peer, 0xf0, 0, 0, 0, 0, 0, 3, 0, //                           the app subscribed, 3 dropped
      defs.stream.target.provider, 0, 2, ...u32(0xffe3), 0, 0, //                     bound to BLE 0xFFE3
      0x10, defs.stream.dir.in, 0, 0, 1, //                                             a user stream, IN
      defs.stream.target.callback, 0, 0, 0, 0, 0, 0, 0xff, 0xff,
    )
    expect(decodeStreamList(defs, list)).toEqual([
      { id: 0x04, dir: 'out', urgent: true, system: true, targets: [{ kind: 'peer', peer: 0xf0, dropped: 3 }, { kind: 'provider', provider: { link: 2, channel: 0xffe3 }, dropped: 0 }] },
      { id: 0x10, dir: 'in', urgent: false, system: false, targets: [{ kind: 'callback', dropped: 0xffff }] },
    ])
    expect(decodeStreamList(defs, list.subarray(0, 8))).toEqual([{ id: 0x04, dir: 'out', urgent: true, system: true, targets: [] }]) // cut short
    expect(decodeStreamList(defs, Uint8Array.of(0))).toEqual([])
  })

  it('reads every session: its provider, what the peer is, open or not, its id, the board\'s timeout of it', () => {
    const peers = Uint8Array.of(
      2,
      2, ...u32(0xffe1), defs.session.peer.client, 1, 0xf0, 0x60, 0xea, //  the app on BLE, open, 60 s
      1, ...u32(1), defs.session.peer.slave, 0, defs.session.id.none, 0xb8, 0x0b, // a UART port to a slave, before its hello
    )
    expect(decodePeers(defs, peers)).toEqual([
      { provider: { link: 2, channel: 0xffe1 }, kind: 'client', open: true, peerId: 0xf0, timeoutMs: 60000 },
      { provider: { link: 1, channel: 1 }, kind: 'slave', open: false, timeoutMs: 3000 },
    ])
  })
})

describe('Streams', () => {
  it('names commands the descriptor has', () => {
    for (const command of Object.values(STREAM_COMMANDS).slice(1)) expect(() => op(STREAM_COMMANDS.service, command)).not.toThrow()
    for (const command of Object.values(SESSION_COMMANDS).slice(1)) expect(() => op(SESSION_COMMANDS.service, command)).not.toThrow()
  })

  it('makes, binds, unbinds and removes a stream as STREAM.MD shows it', async () => {
    const { board, link } = await connected()
    const streams = new Streams(link)
    await streams.create(0x10, { dir: 'in', urgent: true })
    await streams.bind(0x10, { link: 2, channel: 0xee01 })
    await streams.unbind(0x10)
    await streams.remove(0x10)
    const sent = board.requests.map((r) => [...r.subarray(2)])
    expect(sent).toEqual([
      [0x04, defs.dir.write, op('STREAM', 'CREATE'), 0x10, defs.stream.dir.in, defs.stream.flagUrgent],
      [0x04, defs.dir.write, op('STREAM', 'BIND'), 0x10, 2, ...u32(0xee01)],
      [0x04, defs.dir.write, op('STREAM', 'UNBIND'), 0x10, 0, 0, 0, 0, 0], // link 0: every binding
      [0x04, defs.dir.write, op('STREAM', 'REMOVE'), 0x10],
    ])
  })

  it('lists the board\'s streams', async () => {
    const { link } = await connected((b) => {
      b.respond = (frame, send) => send(answer(frame[0]!, [1, 0x03, 0, 0, 1, 0]))
    })
    expect(await new Streams(link).list()).toEqual([{ id: 0x03, dir: 'out', urgent: false, system: true, targets: [] }])
  })

  it('subscribes once for a stream however many handlers, hands its frames over, unsubscribes with the last one', async () => {
    const { board, link } = await connected()
    const streams = new Streams(link)
    const a: number[][] = []
    const b: number[][] = []
    const stopA = await streams.subscribe(0x10, (data) => a.push([...data]))
    const stopB = await streams.subscribe(0x10, (data) => b.push([...data]))
    board.subscribeAnswered = false // from here a SUBSCRIBE is seen like any request
    board.stream(0x10, [7, 8], 1)
    board.stream(0x11, [9], 1) // another stream: not theirs
    await until(() => a.length === 1 && b.length === 1)
    expect(a).toEqual([[7, 8]])
    expect(b).toEqual([[7, 8]])
    expect(board.subscribed.filter((s) => s === 0x10)).toHaveLength(1)
    await stopA()
    expect(board.requests).toHaveLength(0) // one handler left: still subscribed
    await stopB()
    expect([...board.requests.at(-1)!.subarray(2)]).toEqual([0x04, defs.dir.write, op('STREAM', 'UNSUBSCRIBE'), 0x10])
  })

  it('the link\'s own streams only get a handler: the link subscribed them already', async () => {
    const { board, link } = await connected()
    board.subscribeAnswered = false
    const got: number[][] = []
    const stop = await new Streams(link).subscribe(defs.streams.telemetry, (data) => got.push([...data]))
    board.stream(defs.streams.telemetry, [1])
    await until(() => got.length === 1)
    await stop()
    expect(board.requests).toHaveLength(0)
  })

  it('subscribes again after the link comes back: the board dropped the subscriptions with the session', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = new RunitLink(board, { timeoutMs: 200, reconnect: { delaysMs: [0] } })
    await link.connect(board.device)
    const streams = new Streams(link)
    await streams.subscribe(0x10, () => undefined)
    const before = board.subscribed.filter((s) => s === 0x10).length
    board.drop()
    await until(() => board.subscribed.filter((s) => s === 0x10).length === before + 1, 2000)
    expect(link.state).toBe('connected')
    await link.disconnect()
  })
})

describe('Sessions', () => {
  it('reads and sets the board\'s id; an id outside the boards\' is refused before it is sent', async () => {
    const { board, link } = await connected((b) => {
      b.respond = (frame, send) => send(answer(frame[0]!, frame[3] === defs.dir.read ? [0x02] : []))
    })
    const sessions = new Sessions(link)
    expect(await sessions.boardId()).toBe(2)
    await sessions.setBoardId(5)
    expect([...board.requests.at(-1)!.subarray(2)]).toEqual([defs.keys.session!, defs.dir.write, op('SESSION', 'ID'), 5])
    await expect(sessions.setBoardId(defs.session.id.app)).rejects.toThrow(RangeError)
    expect(board.requests).toHaveLength(2)
  })

  it('lists the sessions, attaches a provider to a peer and detaches it', async () => {
    const { board, link } = await connected((b) => {
      b.respond = (frame, send) => send(answer(frame[0]!, frame[4] === op('SESSION', 'PEERS') ? [1, 2, ...u32(0xffe1), defs.session.peer.client, 1, 0xf0, 0x60, 0xea] : []))
    })
    const sessions = new Sessions(link)
    expect(await sessions.peers()).toEqual([{ provider: { link: 2, channel: 0xffe1 }, kind: 'client', open: true, peerId: 0xf0, timeoutMs: 60000 }])
    await sessions.attach({ link: 1, channel: 1 }, 'slave')
    await sessions.detach({ link: 1, channel: 1 })
    expect(board.requests.slice(-2).map((r) => [...r.subarray(2)])).toEqual([
      [defs.keys.session!, defs.dir.write, op('SESSION', 'ATTACH'), 1, ...u32(1), defs.session.peer.slave],
      [defs.keys.session!, defs.dir.write, op('SESSION', 'DETACH'), 1, ...u32(1)],
    ])
  })
})
