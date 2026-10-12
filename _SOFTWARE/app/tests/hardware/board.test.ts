/**
 * The app's backend against the real board over its console UART. Skipped unless RUNIT_PORT is set:
 *
 *   $env:RUNIT_PORT = 'COM9'; npx vitest run tests/hardware
 *
 * (The same code runs over BLE in the app: the RouterClient only needs a FrameLink.)
 */
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, RouterClient } from '../../src/backend/protocol/client'
import type { ErrorRecord } from '../../src/backend/protocol/frames'
import { blockDescriptor, encodeState } from '../../src/backend/vm/blocks'
import { Program } from '../../src/backend/vm/program'
import { VmSession } from '../../src/backend/vm/session'
import { batch, EXEC, KEY_VM, NO_ID, OP, T } from '../../src/backend/vm/wire'
import { UartLink } from './uartLink'

const port = process.env.RUNIT_PORT

describe.runIf(port)('backend on the board', () => {
  let link: UartLink
  let client: RouterClient
  let vm: VmSession
  const errors: ErrorRecord[] = []
  const logs: string[] = []

  beforeAll(async () => {
    link = new UartLink(port!)
    await link.open()
    client = new RouterClient(link, { timeoutMs: 3000 })
    client.onError((record) => errors.push(record))
    client.onLog((line) => logs.push(line))
    vm = new VmSession(client)
    await vm.reset()
  }, 20000)

  afterAll(async () => {
    await vm?.reset().catch(() => undefined)
    client?.close()
    await link?.close()
  })

  it('PING answers the uptime', async () => {
    const data = await client.request([0x01, 0x01])
    expect(data.length).toBe(4)
    expect(new DataView(data.buffer, data.byteOffset).getUint32(0, true)).toBeGreaterThan(0)
  })

  it('an unknown root key is refused, and the root cause is named from the generated catalog', async () => {
    const failed = await client.request([0x7e]).catch((e: unknown) => e)
    expect(failed).toBeInstanceOf(CommandError)
    expect((failed as CommandError).cause_).toBe('ERR_ROUTER_UNKNOWN_KEY')
    expect((failed as CommandError).record).toMatchObject({ source: 'command', link: 1 })
    // the same record is on the errors stream
    await new Promise((r) => setTimeout(r, 300))
    expect(errors.some((r) => client.catalog.rootCause(r) === 'ERR_ROUTER_UNKNOWN_KEY')).toBe(true)
  })

  it('a payload of the wrong size names the command error with its fields', async () => {
    const failed = (await client.request([0x01, 0x04], Uint8Array.of(1, 2)).catch((e: unknown) => e)) as CommandError
    expect(failed.cause_).toBe('ERR_CMD_BAD_LEN')
  })

  it('the board refuses object id 0xFFFF', async () => {
    const record = new Uint8Array(6)
    new DataView(record.buffer).setUint16(0, NO_ID, true)
    record[4] = T.u8
    record[5] = 1
    await client.request([KEY_VM, OP.open], Uint8Array.of(1, 0, 0, 0, 0, 0, 0, 0x10, 0, 0))
    const failed = (await client.request([KEY_VM, OP.addObjects], batch([record])).catch((e: unknown) => e)) as CommandError
    expect(failed.cause_).toBe('ERR_VM_REG_OOB')
    await vm.reset()
  })

  it('loads a program, steps one pass and reads the result (SET copies a fresh value)', async () => {
    const p = new Program()
    const src = p.f32(0, { name: 'src' })
    const dst = p.f32(0, { name: 'dst' })
    const eno = p.bool(false, { name: 'eno' })
    p.block({ type: blockDescriptor('SET').id, inputs: [src, dst], eno, state: encodeState(blockDescriptor('SET'), { value: 0 }) })
    await vm.start(p, EXEC.scan)
    const idle = await vm.step([dst, eno])
    expect([idle.get(dst), idle.get(eno)]).toEqual([0, false]) // nothing fresh yet: SET does nothing
    const stepped = await vm.step([dst, eno], new Map([[src, 3.5]]))
    expect([stepped.get(dst), stepped.get(eno)]).toEqual([3.5, true])
  }, 20000)

  it('a run-time write is read back through a fresh subscription', async () => {
    const p = new Program()
    const a = p.u32(7, { name: 'a' })
    const list = p.obj(T.i32, 4, { name: 'list', init: [1, 2, 3, 4] })
    await vm.start(p, EXEC.normal)
    expect((await vm.snapshot([a, list])).get(a)).toBe(7)
    await vm.write(a, 99)
    await vm.write(list, -5, 2)
    const now = await vm.snapshot([a, list])
    expect(now.get(a)).toBe(99)
    expect(now.get(list)).toEqual([1, 2, -5, 4])
  }, 20000)

  it('a refused load is a CommandError with the board\'s cause', async () => {
    const p = new Program(64)
    p.obj(T.u32, 100) // 400 bytes in a 64-byte arena
    const failed = (await vm.load(p).catch((e: unknown) => e)) as CommandError
    expect(failed).toBeInstanceOf(CommandError)
    expect(failed.cause_).toMatch(/^ERR_VM_/)
    await vm.reset()
  }, 20000)
})
