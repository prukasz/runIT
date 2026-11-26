import { describe, expect, it } from 'vitest'
import { RunitLink } from '@backend'
import type { BleDeviceRef } from '@backend'
import { boardLabel, parseRemembered } from '../../src/app/connection/board'
import type { RememberedBoard } from '../../src/app/connection/board'
import { BoardConnection, NeedsUser } from '../../src/app/connection/connection'
import type { BoardFinder, BoardStore } from '../../src/app/connection/connection'
import { answer, FakeBoard, until } from '../backend/fakes'

const RUNIT: RememberedBoard = { name: 'runit', address: '94:A9:90:DB:32:3E' }

/** A finder whose board is nearby or not (`nearby`), counting its scans; `choice` is what the user picks. */
const finder = (board: FakeBoard) => {
  const calls = { find: 0, cancel: 0, forget: 0 }
  const state = { nearby: false, choice: undefined as RememberedBoard | undefined, needsUser: false }
  const f: BoardFinder = {
    find: async () => {
      calls.find++
      if (state.needsUser) throw new NeedsUser('Choose it again.')
      return state.nearby ? board.device : undefined
    },
    choose: async () => {
      if (!state.choice) throw new Error('User cancelled the requestDevice() chooser.')
      return { ref: board.device, board: state.choice }
    },
    cancel: () => { calls.cancel++ },
    forget: async (_ref: BleDeviceRef | undefined) => { calls.forget++ },
  }
  return { f, calls, state }
}

const memory = (board?: RememberedBoard) => {
  const kept = { board }
  const store: BoardStore = { read: () => kept.board, write: (b) => { kept.board = b } }
  return { store, kept }
}

const setup = (remembered?: RememberedBoard) => {
  const board = new FakeBoard()
  board.respond = (r, send) => send(answer(r))
  const link = new RunitLink(board, { timeoutMs: 300 })
  const { f, calls, state } = finder(board)
  const { store, kept } = memory(remembered)
  const connection = new BoardConnection(link, f, store, 10)
  return { board, link, connection, calls, state, kept }
}

describe('the remembered board', () => {
  it('is its name and its address (upper case) or the browser\'s id; anything else is none', () => {
    expect(parseRemembered('{"name":"runit","address":"94:a9:90:db:32:3e"}')).toEqual(RUNIT)
    expect(parseRemembered('{"name":"runit","browserId":"abc"}')).toEqual({ name: 'runit', browserId: 'abc' })
    expect(parseRemembered('{"name":"runit","address":"nope"}')).toBeUndefined()
    expect(parseRemembered('{"id":"x","name":"runit","scanId":"94:A9:90:DB:32:3E"}')).toBeUndefined()
    expect(boardLabel(RUNIT)).toBe('runit (94:A9:90:DB:32:3E)')
  })
})

describe('connecting automatically', () => {
  it('looks for the remembered board until it is nearby, then connects', async () => {
    const { connection, calls, state, link } = setup(RUNIT)
    connection.auto()
    await until(() => calls.find >= 3)
    expect(connection.current.searching).toBe(true)
    expect(connection.current.reason).toMatch(/not nearby/)
    state.nearby = true
    await until(() => link.state === 'connected', 2000)
    expect(connection.current).toMatchObject({ link: 'connected', searching: false })
    expect(connection.current.reason).toBeUndefined()
    await connection.disconnect()
    connection.close()
  })

  it('does nothing without a remembered board', async () => {
    const { connection, calls } = setup()
    connection.auto()
    await new Promise((r) => setTimeout(r, 30))
    expect(calls.find).toBe(0)
    connection.close()
  })

  it('Disconnect ends the search: no scan after it', async () => {
    const { connection, calls } = setup(RUNIT)
    connection.auto()
    await until(() => calls.find >= 2)
    await connection.disconnect()
    const scans = calls.find
    await new Promise((r) => setTimeout(r, 50))
    expect(calls.find).toBe(scans)
    expect(calls.cancel).toBeGreaterThan(0)
    expect(connection.current.searching).toBe(false)
    connection.close()
  })

  it('a board only the user can find again stops the search with why; Connect then shows the chooser', async () => {
    const { connection, state, link, kept } = setup({ name: 'runit', browserId: 'old' })
    state.needsUser = true
    connection.auto()
    await until(() => connection.current.reason === 'Choose it again.')
    expect(connection.current.searching).toBe(false)
    state.choice = { name: 'runit', browserId: 'new' }
    await connection.connect()
    await until(() => link.state === 'connected', 2000)
    expect(kept.board).toEqual({ name: 'runit', browserId: 'new' })
    await connection.disconnect()
    connection.close()
  })
})

describe('Connect and Forget', () => {
  it('with no board remembered the user picks one: it is remembered and connected', async () => {
    const { connection, state, link, kept } = setup()
    state.choice = RUNIT
    await connection.connect()
    await until(() => link.state === 'connected', 2000)
    expect(kept.board).toEqual(RUNIT)
    expect(connection.current.board).toEqual(RUNIT)
    await connection.disconnect()
    connection.close()
  })

  it('a cancelled choice is no error', async () => {
    const { connection } = setup()
    await connection.connect()
    expect(connection.current.reason).toBeUndefined()
    expect(connection.current.choosing).toBe(false)
    connection.close()
  })

  it('Forget drops the link, the permission and the remembered board', async () => {
    const { connection, state, link, kept, calls } = setup(RUNIT)
    state.nearby = true
    await connection.connect()
    await until(() => link.state === 'connected', 2000)
    await connection.forget()
    expect(link.state).toBe('idle')
    expect(kept.board).toBeUndefined()
    expect(connection.current.board).toBeUndefined()
    expect(calls.forget).toBe(1)
    connection.close()
  })
})
