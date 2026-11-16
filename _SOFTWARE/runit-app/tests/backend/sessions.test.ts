import { describe, expect, it } from 'vitest'
import { decodePeers, RunitLink, SESSION_COMMANDS, Sessions } from '@backend'
import type { LinkDiagnostic, StreamFrame } from '@backend'
import { answer, CLIENT_ID, defs, FakeBoard, until } from './fakes'

/*
 * The session as the firmware has it (SYS_SESSION.MD): each side says its own id in its hello, the board's streams and a module's
 * channels come as PUSH frames by their first byte, and the board's own sessions are SESSION PEERS. And the module over them: Sessions.
 */

const connected = async (setup?: (board: FakeBoard) => void) => {
  const board = new FakeBoard()
  board.respond = (frame, send) => send(answer(frame[0]!))
  setup?.(board)
  const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
  await link.connect(board.device)
  return { board, link }
}

describe('the session', () => {
  it("says the app's own id in its hello and takes the board's id, boot id and build from the board's", async () => {
    const { board, link } = await connected((b) => (b.boardId = 3))
    expect(board.clientHello).toMatchObject({ id: defs.session.id.app, buildId: 0 })
    expect(link.session).toMatchObject({ clientId: CLIENT_ID, boardId: 3, bootId: 1, buildId: 0xb17d1d, deadmanMs: 0 })
    await link.close()
  })

  it('a client that picks its own id says that one', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: false, clientId: defs.session.id.clientFirst + 1 })
    await link.connect(board.device)
    expect(board.clientHello?.id).toBe(defs.session.id.clientFirst + 1)
    expect(link.session?.clientId).toBe(defs.session.id.clientFirst + 1)
    await link.close()
  })

  it('subscribes nothing: the board routes its errors and events to the session by itself', async () => {
    const { board, link } = await connected()
    expect(board.requests).toEqual([]) // SESSION PEERS is the link\'s own and the fake answers it itself
    await link.close()
  })

  it('hands the board\'s streams and a module\'s channels over by name', async () => {
    const { board, link } = await connected()
    const got: StreamFrame[] = []
    link.onStream((f) => got.push(f))
    board.stream(defs.channels.watches, [1, 0, 9], 4)
    board.stream(defs.channels.remote, [0x05, 0x00, 0, 0xaa], 2)
    board.stream(defs.streams.errors, [0x01])
    board.stream(defs.streams.events, [0x02])
    await until(() => got.length === 4)
    expect(got.map((f) => [f.name, f.stream, f.seq])).toEqual([['watches', defs.channels.watches, 4], ['remote', defs.channels.remote, 2], ['errors', defs.streams.errors, 0], ['events', defs.streams.events, 0]])
    expect([...got[1]!.data]).toEqual([0x05, 0x00, 0, 0xaa])
    await link.close()
  })

  it('a tag nobody publishes is a diagnostic, not a stream by name', async () => {
    const { board, link } = await connected()
    const diagnostics: LinkDiagnostic[] = []
    link.onDiagnostic((d) => diagnostics.push(d))
    board.stream(0x10, [1])
    await until(() => diagnostics.length === 1)
    expect(diagnostics).toEqual([{ kind: 'unknown-stream', stream: 0x10 }])
    await link.close()
  })

  it('with the dead-man on, the link sends a PING before a third of its time is gone', async () => {
    const { board, link } = await connected((b) => (b.deadmanMs = 300))
    expect(link.session?.deadmanMs).toBe(300)
    const before = board.pings
    await until(() => board.pings >= before + 2, 700)
    await link.close()
  })

  it('with no dead-man, nothing is sent while idle', async () => {
    const { board, link } = await connected()
    await new Promise((r) => setTimeout(r, 300))
    expect(board.pings).toBe(0)
    await link.close()
  })

  it('a new boot id in the board\'s hello is a restart: the session opens again', async () => {
    const { board, link } = await connected()
    const states: string[] = []
    link.onStatus((s) => states.push(s.state))
    board.bootId = 2
    board.notify(board.hello())
    await until(() => states.includes('connected'), 1000)
    expect(states).toEqual(['reconnecting', 'connected'])
    expect(link.session?.bootId).toBe(2)
    await link.close()
  })
})

describe('SESSION PEERS, as the firmware packs it (sys_session_info_t, 10 B)', () => {
  it('reads every session: its provider, open or not, its id, the dead-man and the build', () => {
    const row = (provider: number, open: number, peer: number, safety: number, build: number, deadman: number) => [provider, open, peer, safety, build & 0xff, (build >> 8) & 0xff, (build >> 16) & 0xff, build >>> 24, deadman & 0xff, deadman >> 8]
    const data = Uint8Array.from([2, ...row(1, 1, 0xf0, 1, 0x11223344, 1500), ...row(2, 0, 0xfd, 0, 0, 0)])
    expect(decodePeers(defs, data)).toEqual([
      { provider: 1, open: true, peerId: 0xf0, safety: true, buildId: 0x11223344, deadmanMs: 1500 },
      { provider: 2, open: false, safety: false, buildId: 0, deadmanMs: 0 },
    ])
    expect(decodePeers(defs, data.subarray(0, 15))).toHaveLength(1) // a record cut short ends the list
  })
})

describe('Sessions', () => {
  it('names commands the descriptor has', async () => {
    const { link } = await connected()
    const { catalog } = new (await import('@backend')).Services(link)
    for (const command of [SESSION_COMMANDS.id, SESSION_COMMANDS.peers]) expect(catalog.service(SESSION_COMMANDS.service).commands.has(command), command).toBe(true)
    await link.close()
  })

  it('reads and sets the board id, in 0..the last board id', async () => {
    const { board, link } = await connected()
    board.respond = (frame, send) => send(answer(frame[0]!, [frame[3] === defs.dir.read ? 5 : 0].filter((_, i) => frame[3] === defs.dir.read && i === 0)))
    const sessions = new Sessions(link)
    expect(await sessions.boardId()).toBe(5)
    await sessions.setBoardId(2)
    await expect(sessions.setBoardId(defs.session.id.boardLast + 1)).rejects.toThrow(RangeError)
    await link.close()
  })
})
