import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, decodeErrorRecord, serviceCall } from '@backend'
import type { ErrorRecord } from '@backend'
import { defs, openBoard, until } from './harness'
import type { Board } from './harness'

/* How the board runs commands, seen over BLE (ROUTER.MD, ROUTER_QUEUE.MD): in order, NOREPLY, the size of a request. The pin is a free one of the devkit. */

const PIN = 4
let board: Board
beforeAll(async () => {
  board = await openBoard()
  await board.services.device(0, 'IO', 'RESET', { pin: PIN }) // a pin a run before set up is set up again
  await board.services.device(0, 'IO', 'MODE', { pin: PIN, mode: 'SYS_IO_MODE_OUTPUT_PUSH_PULL' }, { side: 'write' })
  await board.services.device(0, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' })
})
afterAll(async () => {
  await board.close()
})

const level = async () => String(((await board.services.device(0, 'IO', 'LEVEL', { pin: PIN }, { side: 'read' })).value as { level: unknown }).level)

describe('order', () => {
  it('9 pipelined TOGGLEs leave the pin high, one more low: the board runs normal commands in the order they came', async () => {
    const toggle = () => board.services.device(0, 'IO', 'TOGGLE', { pin: PIN })
    await Promise.all(Array.from({ length: 9 }, toggle))
    expect(await level()).toMatch(/High|1|true/i)
    await toggle()
    expect(await level()).toMatch(/Low|0|false/i)
  })
})

describe('no reply', () => {
  it('a NOREPLY request is done once written; its failure goes to the errors stream only, naming this client and the flag', async () => {
    const records: ErrorRecord[] = []
    const off = board.link.on('errors', (data) => {
      const record = decodeErrorRecord(defs, data)
      if (record) records.push(record)
    })
    const call = serviceCall(board.services.catalog, 'SYS', 'PING')
    const t0 = performance.now()
    const done = await board.link.request({ ...call.request, keys: [...call.request.keys.slice(0, 2), 0x7f], noReply: true }) // an op the board does not have
    expect(done.length).toBe(0)
    expect(performance.now() - t0).toBeLessThan(500)
    const record = await until(() => records.find((r) => r.source === 'command' && r.command?.request[0] === call.request.keys[0]), 3000)
    off()
    expect(record).toBeDefined()
    expect(record!.command).toMatchObject({ sender: board.link.session!.clientId, flags: defs.flags.noReply })
  })
})

describe('size', () => {
  it('a request of the largest size is read to its last byte (ERR_CMD_BAD_LEN says how many it got), one byte more is refused by the link', async () => {
    const max = defs.frameMax - defs.frameHead
    const keys = [defs.keys.sys!, defs.dir.read, serviceCall(board.services.catalog, 'SYS', 'PING').request.keys[2]!]
    const error = await board.link.request({ keys, payload: new Uint8Array(max - keys.length) }).then(() => undefined, (e: unknown) => e as CommandError)
    expect(error?.code).toBe('device-error')
    expect(error?.record?.cause?.fields).toMatchObject({ got: max - keys.length, need: 0 })
    await expect(board.link.request({ keys, payload: new Uint8Array(max - keys.length + 1) })).rejects.toMatchObject({ code: 'too-long' })
  })
})
