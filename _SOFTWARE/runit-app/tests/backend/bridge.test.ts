import { describe, expect, it } from 'vitest'
import { BRIDGE_DEVICE, BRIDGE_PROVIDER, BridgePlatform, bundledServiceCatalog, DEFAULT_WIRE, REMOTE_BOARD, RunitLink, serviceCall } from '@backend'
import { answer, defs, FakeBoard, until } from './fakes'

/*
 * The link to the board behind the main one (backend/session/bridge.ts): the UART and its two links made on the main board,
 * then a session of its own through the main board's bridge, beside the main board's session.
 */

const catalog = bundledServiceCatalog()
const OPTIONS = { timeoutMs: 100, reconnect: { delaysMs: [0, 1, 2], openTimeoutMs: 100 } }

/** The main board answering everything OK with no data (its LIST: no devices) and the board behind it echoing a request's arguments. */
const boards = () => {
  const main = new FakeBoard()
  const behind = new FakeBoard()
  behind.boardId = 0x02
  main.behind = behind
  main.respond = (r, send) => send(answer(r))
  behind.respond = (r, send) => send(answer(r, r.data.subarray(2), true, behind.boardId))
  return { main, behind }
}

const connected = async () => {
  const { main, behind } = boards()
  const master = new RunitLink(main, OPTIONS)
  await master.connect(main.device)
  const platform = new BridgePlatform(master, () => DEFAULT_WIRE, catalog)
  const remote = new RunitLink(platform, OPTIONS)
  await remote.connect(REMOTE_BOARD, { retry: true })
  return { main, behind, master, remote, platform }
}

describe('the board behind the main one', () => {
  it('makes the UART on the main board, links it with the bridge both ways, and opens a session with the board behind', async () => {
    const { main, behind, master, remote, platform } = await connected()
    expect(remote.session?.boardId).toBe(behind.boardId)
    expect(master.session?.boardId).toBe(main.boardId)
    expect(platform.heard).toBeGreaterThan(0)
    const matrix = main.requests.filter((r) => r.key === defs.keys.data_matrix)
    const port = (provider: number) => (defs.providerPortKind << 8) | provider
    const link = (from: number, to: number) => [...serviceCall(catalog, 'DATA_MATRIX', 'CONNECT', { from, to }, 'write').request.body]
    expect(matrix.map((r) => [...r.data])).toEqual([link(port(BRIDGE_PROVIDER), port(0x40 + BRIDGE_DEVICE)), link(port(0x40 + BRIDGE_DEVICE), port(BRIDGE_PROVIDER))])
    // The UART's CREATE came before the links, under the bridge's ID, with the wire's pins.
    const create = main.requests.find((r) => r.key === defs.keys.sys_device && r.data[0] === defs.dir.write && r.data[1] === BRIDGE_DEVICE)!
    expect(main.requests.indexOf(create)).toBeLessThan(main.requests.indexOf(matrix[0]!))
    expect([...create.data].join(',')).toContain([0, DEFAULT_WIRE.tx, 0, DEFAULT_WIRE.rx].join(','))
    await remote.close()
    await master.close()
  })

  it('each link asks its own board', async () => {
    const { main, behind, master, remote } = await connected()
    const before = main.requests.length
    const ask = { key: defs.keys.sys!, body: Uint8Array.from([defs.dir.read, 1, 7, 8, 9]) }
    expect([...(await remote.request(ask))]).toEqual([7, 8, 9])
    expect(behind.requests).toHaveLength(1)
    expect(main.requests).toHaveLength(before)
    await master.request(ask)
    expect(behind.requests).toHaveLength(1)
    await remote.close()
    await master.close()
  })

  it('goes down with the main board and comes back with it, the UART made again', async () => {
    const { main, master, remote } = await connected()
    const links = () => main.requests.filter((r) => r.key === defs.keys.data_matrix).length
    const states: string[] = []
    remote.onStatus((s) => states.push(s.state))
    main.drop()
    await until(() => master.state === 'connected' && remote.state === 'connected' && links() === 4, 2000)
    expect(states).toContain('reconnecting')
    await remote.close()
    await master.close()
  })

  it('does not open while the main board is not connected', async () => {
    const { main } = boards()
    const platform = new BridgePlatform(new RunitLink(main, OPTIONS), () => DEFAULT_WIRE, catalog)
    await expect(new RunitLink(platform, OPTIONS).connect(REMOTE_BOARD)).rejects.toThrow('main board is not connected')
  })

  it('attachSlave creates the UART and attaches a SYS_SESSION_SLAVE session on the master', async () => {
    const { main } = boards()
    const master = new RunitLink(main, OPTIONS)
    await master.connect(main.device)
    const platform = new BridgePlatform(master, () => DEFAULT_WIRE, catalog)
    await platform.attachSlave()
    const create = main.requests.find((r) => r.key === defs.keys.sys_device && r.data[0] === defs.dir.write && r.data[1] === BRIDGE_DEVICE)
    expect(create).toBeDefined()
    const expectedAttach = serviceCall(catalog, 'SESSION', 'ATTACH', { provider: 0x40 + BRIDGE_DEVICE, flags: 0x02, deadman_ms: 0, modules: Uint8Array.from([]) }, 'write').request
    const attach = main.requests.find((r) => r.key === expectedAttach.key && [...r.data].join(',') === [...expectedAttach.body].join(','))
    expect(attach).toBeDefined()
    await master.close()
  })
})
