import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, CommandError, leaveSafeState, RunitLink, SAFE_STATE_COMMANDS } from '@backend'
import { engine } from '../engine/helpers'
import { answer, defs, FakeBoard } from './fakes'

const catalog = bundledServiceCatalog()
const hex = (b: Uint8Array) => [...b].map((x) => x.toString(16).padStart(2, '0')).join('')

const linkTo = async (board: FakeBoard) => {
  const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
  await link.connect(board.device)
  return link
}

/** The keys and payload of a request the board received (without seq and flags). */
const body = (frame: Uint8Array) => hex(frame.subarray(2))

describe('leaving the safe state', () => {
  it('names commands the descriptors have', () => {
    expect(catalog.contracts.get(SAFE_STATE_COMMANDS.devices)!.commands.has(SAFE_STATE_COMMANDS.resume)).toBe(true)
    expect(catalog.service(SAFE_STATE_COMMANDS.vm).commands.has(SAFE_STATE_COMMANDS.exec)).toBe(true)
    expect(catalog.allDevices).toBe(0xffff) // SYS_DEVICE_ID_NONE, sys_device.h
  })

  it('leaves by SYS SAFE_LEAVE (every device resumed, the note cleared), then starts the program with the EXEC given', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = await linkTo(board)
    const start = engine.exec('normal')
    await leaveSafeState(link, { start })
    expect(board.requests.map(body)).toEqual([hex(Uint8Array.of(defs.keys.sys!, 0x0a)), hex(Uint8Array.of(defs.keys.vm!, start.op, ...start.payload))])
    await link.close()
  })

  it('a firmware without SAFE_LEAVE gets the RESUME sweep ([05][FF FF][03 00], SYS_DEVICE.MD); without a start the VM stays stopped', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [], frame[2] !== defs.keys.sys))
    const link = await linkTo(board)
    await leaveSafeState(link)
    expect(board.requests.map(body)).toEqual([hex(Uint8Array.of(defs.keys.sys!, 0x0a)), hex(Uint8Array.of(defs.keys.device!, 0xff, 0xff, 0x03, 0x00))])
    await link.close()
  })

  it('a failed resume is thrown as it is and the program is not started', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [], false))
    const link = await linkTo(board)
    const error = await leaveSafeState(link, { start: engine.exec('normal') }).catch((e: unknown) => e)
    expect(error).toBeInstanceOf(CommandError)
    expect((error as CommandError).code).toBe('device-error')
    expect(board.requests).toHaveLength(2)
    await link.close()
  })

  it('refuses a start that is not EXEC, sending nothing', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    await expect(leaveSafeState(link, { start: engine.subscribe([]) })).rejects.toThrow(/VM EXEC/)
    expect(board.requests).toHaveLength(0)
    await link.close()
  })
})
