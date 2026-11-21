import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, decodePeers, RunitLink, SESSION_COMMANDS, Services, Sessions } from '@backend'
import { answer, CLIENT_ID, defs, FakeBoard } from './fakes'

/*
 * The session as the firmware has it (SYS_SESSION.MD): each side says its own id as the source of its hello, and the board's own
 * view of its sessions is SESSION PEERS, laid out by its descriptor. And the module over them: Sessions.
 */

const catalog = bundledServiceCatalog()

const connected = async (setup?: (board: FakeBoard) => void) => {
  const board = new FakeBoard()
  board.respond = (r, send) => send(answer(r))
  setup?.(board)
  const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
  await link.connect(board.device)
  return { board, link }
}

describe('the session', () => {
  it("says the app's own id in its hello and takes the board's id, boot id and build from the board's", async () => {
    const { board, link } = await connected((b) => (b.boardId = 3))
    expect(board.clientHello).toMatchObject({ id: CLIENT_ID, buildId: 0 })
    expect(link.session).toMatchObject({ clientId: CLIENT_ID, boardId: 3, bootId: 1, buildId: 0xb17d1d, deadmanMs: 0 })
    await link.close()
  })

  it('a client that picks its own id says that one', async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { reconnect: false, clientId: defs.src.clientFirst + 1 })
    await link.connect(board.device)
    expect(board.clientHello?.id).toBe(defs.src.clientFirst + 1)
    expect(link.session?.clientId).toBe(defs.src.clientFirst + 1)
    await link.close()
  })

  it('subscribes nothing: the board links its errors and events to the session by itself', async () => {
    const { board, link } = await connected()
    expect(board.requests).toEqual([]) // SESSION PEERS is the link's own and the fake answers it itself
    await link.close()
  })

  it('with no dead-man, nothing is sent while idle', async () => {
    const { board, link } = await connected()
    await new Promise((r) => setTimeout(r, 200))
    expect(board.pings).toBe(0)
    await link.close()
  })
})

describe('SESSION PEERS, as its descriptor lays it out', () => {
  it('its rows are sys_session_info_t (10 B) after a count', () => {
    const returns = catalog.command(catalog.service(SESSION_COMMANDS.service), SESSION_COMMANDS.peers).read!.returns!
    expect(returns.kind === 'struct' && returns.layout.fixed).toBe(10)
    expect(returns.lead?.rawType).toBe('u8')
    expect(returns.kind === 'struct' && returns.layout.fields.map((f) => f.id)).toEqual(Object.values(SESSION_COMMANDS.row))
  })

  it('reads every session: its provider, open or not, its id, the dead-man and the build', () => {
    const row = (provider: number, open: number, peer: number, safety: number, build: number, deadman: number) => [provider, open, peer, safety, build & 0xff, (build >> 8) & 0xff, (build >> 16) & 0xff, build >>> 24, deadman & 0xff, deadman >> 8]
    const data = Uint8Array.from([2, ...row(1, 1, 0xf0, 1, 0x11223344, 1500), ...row(2, 0, defs.src.none, 0, 0, 0)])
    expect(decodePeers(data, catalog)).toEqual([
      { provider: 1, open: true, peerId: 0xf0, safety: true, buildId: 0x11223344, deadmanMs: 1500 },
      { provider: 2, open: false, safety: false, buildId: 0, deadmanMs: 0 },
    ])
  })
})

describe('Sessions', () => {
  it('names commands the descriptor has', () => {
    for (const command of [SESSION_COMMANDS.id, SESSION_COMMANDS.peers]) expect(catalog.service(SESSION_COMMANDS.service).commands.has(command), command).toBe(true)
  })

  it('reads and sets the board id; the descriptor holds it to 0..the last board id', async () => {
    const { board, link } = await connected()
    board.respond = (r, send) => send(answer(r, r.data[0] === defs.dir.read ? [5] : []))
    const sessions = new Sessions(link)
    expect(await sessions.boardId()).toBe(5)
    await sessions.setBoardId(2)
    await expect(sessions.setBoardId(defs.src.boardLast + 1)).rejects.toThrow(/above/)
    await link.close()
  })

  it('peers() reads the board\'s sessions', async () => {
    const { link } = await connected()
    expect(await new Sessions(link, new Services(link, catalog)).peers()).toEqual([{ provider: 3, open: true, peerId: CLIENT_ID, safety: false, buildId: 0, deadmanMs: 0 }])
    await link.close()
  })
})
