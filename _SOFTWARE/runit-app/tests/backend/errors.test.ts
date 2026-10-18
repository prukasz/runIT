import { describe, expect, it } from 'vitest'
import {
  bundledEnums, combineResolvers, CommandError, deviceRefs, ErrorInterpreter, ErrorLog, errorCatalog, errorIdKinds, programRefs, refKey, RunitLink,
} from '@backend'
import type { ErrorReport } from '@backend'
import { blk, build, num, project, read } from '../engine/helpers'
import { defs, FakeBoard, until } from './fakes'

const catalog = errorCatalog()
const kinds = errorIdKinds()
const tagOf = (name: string) => [...catalog.values()].find((e) => e.name === name)!.tag

const u16 = (n: number) => [n & 0xff, n >> 8]
const u32 = (n: number) => [n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, n >>> 24]

/** The payload of an error from its field values, laid out as errors.json says. */
const payload = (name: string, values: Record<string, number> = {}): number[] => {
  const info = catalog.get(tagOf(name))!
  const out = new Array<number>(info.fields.reduce((n, f) => Math.max(n, f.offset + f.size), 0)).fill(0)
  for (const f of info.fields) {
    const v = values[f.name] ?? 0
    const bytes = f.size === 1 ? [v & 0xff] : f.size === 2 ? u16(v) : u32(v)
    out.splice(f.offset, f.size, ...bytes)
  }
  return out
}

/** An error record (ROUTER.MD "Error record"): a command's when `request` is given, else an internal one. */
const record = (nodes: [string, Record<string, number>?][], request?: number[], seq = 7, time = 0): Uint8Array => {
  const stamp = [...u32(time), ...u32(0)]  // u64 LE
  const head = request ? [defs.errorRecord.srcCommand, ...stamp, 1, ...u32(0), seq, 0, request.length, ...request] : [defs.errorRecord.srcInternal, ...stamp]
  const chain = nodes.flatMap(([name, values]) => {
    const info = catalog.get(tagOf(name))!
    const p = payload(name, values)
    return [...u32(info.tag), info.severity, p.length, ...p]
  })
  return Uint8Array.from([...head, nodes.length, ...chain])
}

const ioMode = bundledEnums().sys_io_mode_e!.members[0]!

describe('errors.json', () => {
  it('every error has a message whose {fields} are its fields, and every id kind is described', () => {
    const wrong: string[] = []
    for (const e of catalog.values()) {
      if (!e.message) wrong.push(`${e.name}: no message`)
      for (const [, name] of e.message.matchAll(/\{([^{}]*)\}/g)) if (!e.fields.some((f) => f.name === name)) wrong.push(`${e.name}: {${name}}`)
      for (const f of e.fields) {
        for (const kind of [f.id_kind, ...Object.values(f.id_kind_by?.kinds ?? {})]) if (kind && !kinds.has(kind)) wrong.push(`${e.name}.${f.name}: ${kind}`)
        if (f.of && !e.fields.some((o) => o.name === f.of)) wrong.push(`${e.name}.${f.name}: of ${f.of}`)
      }
    }
    for (const [kind, info] of kinds) if (info.scope && !kinds.has(info.scope)) wrong.push(`${kind}: scope ${info.scope}`)
    expect(wrong).toEqual([])
    expect(catalog.size).toBeGreaterThan(150)
  })

  it('context nodes are the ones of severity NONE', () => {
    for (const e of catalog.values()) expect(e.context, e.name).toBe(e.severity === 0)
  })
})

describe('reading an error', () => {
  const interpreter = new ErrorInterpreter({ resolve: deviceRefs(new Map([[3, 'Left motor']])) })

  it('a device command: the route, the device and its pin, the message with names, enum aliases and the command', () => {
    const op = 0x0101 // IO contract, op 1
    const data = record(
      [['ERR_ROUTER_KEY', { key: defs.keys.device! }], ['ERR_DEV_ID', { id: 3 }], ['ERR_DEV_OP', { op }], ['ERR_IO_PIN_IN_USE', { id: 3, pin: 22, mode: ioMode.value }]],
      [defs.keys.device!, ...u16(3), ...u16(op), 22],
    )
    const report = interpreter.read(data)!
    expect(report.message).toBe(`Pin 22 of Left motor is already set up as ${ioMode.alias}`)
    expect(report.where).toHaveLength(3)
    expect(report.where[1]).toBe('In Left motor')
    expect(report.where[2]).toMatch(/^In IO/)
    expect(report.command!.label).toMatch(/\(Left motor\)$/)
    expect(report.refs.map(refKey)).toEqual(expect.arrayContaining(['device:3', 'device:3/device_op:257', 'device:3/pin:22', `router_key:${defs.keys.device}`]))
    expect(report.cause!.fields.find((f) => f.name === 'pin')!.ref).toEqual({ kind: 'pin', id: 22, scope: { kind: 'device', id: 3 } })
    expect(interpreter.target(report.cause!.fields[1]!.ref!)).toEqual({ kind: 'pin', device: 3, pin: 22 })
    expect(report.severity).toBe(catalog.get(tagOf('ERR_IO_PIN_IN_USE'))!.severity)
    // What to do about it, from errors_<module>.json; an error without one has none.
    expect(report.cause!.hint).toMatch(/Reset the pin/)
    expect(report.chain[0]!.hint).toBeUndefined()
  })

  it('a scoped id without its instance in the node takes the nearest one before it in the chain', () => {
    const report = interpreter.read(record([['ERR_DEV_ID', { id: 3 }], ['ERR_DEV_UNKNOWN_OP', { op: 0x0907 }]]))!
    expect(report.cause!.refs).toEqual([{ kind: 'device_op', id: 0x0907, scope: { kind: 'device', id: 3 } }])
  })

  it('a field whose kind another field selects (id_kind_by), and a bare number in the kind\'s format', () => {
    const report = interpreter.read(record([['ERR_VM_BLK_BAD_REF', { blk_id: 4, ref_id: 9, slot: 1, kind: 0 }], ['ERR_I2C_DEV_NOT_FOUND', { bus: 0, addr: 0x3c }]]))!
    const [bad, i2c] = report.chain
    expect(bad!.refs.map(refKey).sort()).toEqual(['vm_accessor:9', 'vm_block:4', 'vm_block:4/vm_block_input:1'])
    expect(i2c!.message).toBe('Nothing answers at I2C address 0x3C on I2C bus 0')
  })

  it('names from the descriptors: device types, error tags', () => {
    const report = interpreter.read(record([['ERR_DEV_TYPE_UNKNOWN', { type: 0x41 }], ['ERR_VM_EXEC_FAULT_LATCHED', { device_id: 3, root_tag: tagOf('ERR_DEV_SUSPENDED') }]]))!
    expect(report.chain[0]!.message).not.toMatch(/Device type 65/)
    expect(report.chain[1]!.message).toBe('The program is stopped by a critical error of Left motor (ERR_DEV_SUSPENDED): acknowledge it to start again')
  })

  it('a tag errors.json does not have keeps its number', () => {
    const report = interpreter.read(Uint8Array.from([defs.errorRecord.srcInternal, ...u32(0), ...u32(0), 1, ...u32(0x7fff0001), 3, 0]))!
    expect(report.cause).toMatchObject({ known: false, name: '0x7FFF0001', message: 'Error 0x7FFF0001' })
  })
})

describe('the program behind an error', () => {
  const p = project([num('x', 'speed'), num('y', 'limit')], [blk('e', 'expr', { name: 'Scale', groups: { inputs: 2 }, inputs: [read('x'), read('y')], editor: { text: 'in0 * 2 + in1' } })])
  const result = build(p)
  const interpreter = new ErrorInterpreter({ resolve: combineResolvers(programRefs(p, result), deviceRefs(new Map())) })
  const blockId = result.ids.blocks.get('e')!

  it('variables, blocks and block pins by name, pointing at the project', () => {
    const report = interpreter.read(record([['ERR_VM_BLOCK_PIN_UNLINKED', { block_idx: blockId, pin_id: 1, is_out: 0 }], ['ERR_VM_OBJ_NOT_MUTABLE', { obj_id: result.ids.variables.get('x')! }]]))!
    expect(report.chain[0]!.message).toBe('input 2 of Scale is not connected')
    expect(report.chain[1]!.message).toBe('speed cannot be written')
    const pin = report.chain[0]!.fields.find((f) => f.name === 'pin_id')!.ref!
    expect(interpreter.target(pin)).toEqual({ kind: 'block', block: 'e', pin: { kind: 'input', index: 1 } })
    expect(interpreter.target(report.chain[1]!.refs[0]!)).toEqual({ kind: 'variable', variable: 'x' })
  })

  it('an accessor is the block pin that reads through it', () => {
    const acc = result.ir!.blocks.find((b) => b.id === blockId)!.inputs[0]!
    const report = interpreter.read(record([['ERR_VM_ACCESSOR_UNKNOWN_ID', { id: acc }]]))!
    expect(report.message).toBe('input 1 of Scale does not exist')
    expect(interpreter.target(report.refs[0]!)).toEqual({ kind: 'block', block: 'e', pin: { kind: 'input', index: 0 } })
  })
})

describe('the error log', () => {
  const interpreter = new ErrorInterpreter({ resolve: deviceRefs(new Map([[3, 'Left motor']])) })
  const clock = () => {
    let t = 1000
    return { now: () => t, tick: (ms: number) => (t += ms) }
  }
  const pinError = (seq: number, pin = 22) => record([['ERR_DEV_ID', { id: 3 }], ['ERR_IO_PIN_UNCONFIGURED', { id: 3, pin }]], [defs.keys.device!, ...u16(3), 1, 1], seq)

  it('an answer and its copy on the stream are one; the same error again counts up and is unread again', () => {
    const c = clock()
    const log = new ErrorLog(interpreter, { now: c.now })
    log.addBytes(pinError(7))
    c.tick(5)
    log.addBytes(pinError(7)) // the same record on the stream
    expect(log.entries()).toHaveLength(1)
    expect(log.entries()[0]!.count).toBe(1)
    log.markRead()
    expect(log.unread()).toBe(0)
    c.tick(5)
    log.addBytes(pinError(8)) // it happened again
    expect(log.entries()).toHaveLength(1)
    expect(log.entries()[0]).toMatchObject({ count: 2, read: false, first: 1000, last: 1010 })
  })

  it("dates an entry by the board's clock in its record: UTC as it is, a time since the start once the clock is known, never twice for one record", () => {
    const stamp = (ms: number) => [...u32(ms % 2 ** 32), ...u32(Math.floor(ms / 2 ** 32))]
    const rec = (ms: number, seq: number) => {
      const bytes = record([['ERR_DEV_ID', { id: 3 }], ['ERR_IO_PIN_UNCONFIGURED', { id: 3, pin: 22 }]], [defs.keys.device!, ...u16(3), 1, 1], seq, 0)
      bytes.set(stamp(ms), 1)
      return bytes
    }
    const log = new ErrorLog(new ErrorInterpreter(), { now: () => 5 })
    const utc = 1_790_000_000_000
    // a record from before the clock was set, the board's clock not known yet: dated by arrival
    expect(log.addBytes(rec(1200, 1))!.last).toBe(5)
    log.clear()
    // the clock known (the board's clock 2026-…, 60 s after its start): a time since the start becomes UTC
    log.setClock(utc, 60_000)
    expect(log.addBytes(rec(1200, 2))!.last).toBe(utc - 60_000 + 1200)
    // a record with a date is that date; the same record read again (the board's history) is not a second one
    const dated = log.addBytes(rec(utc - 10_000, 3))!
    expect(dated.last).toBe(utc - 10_000)
    expect(log.addBytes(rec(utc - 10_000, 3))!.count).toBe(dated.count)
    expect(log.entries()).toHaveLength(1)
  })

  it('finds and clears entries by the instance they are about; clearRead removes what was read', () => {
    const log = new ErrorLog(interpreter)
    log.addBytes(pinError(1, 22))
    log.addBytes(pinError(2, 23))
    log.addBytes(record([['ERR_I2C_BUS_STUCK', { bus: 1, addr: 0x40 }]]))
    expect(log.entries({ ref: 'device:3' })).toHaveLength(2) // the device takes its pins' errors
    expect(log.entries({ ref: { kind: 'pin', id: 23, scope: { kind: 'device', id: 3 } } })).toHaveLength(1)
    expect(log.unread('i2c_bus:1')).toBe(1)
    log.markRead({ ref: 'device:3' })
    expect(log.clearRead()).toBe(2)
    expect(log.entries().map((e) => e.report.cause!.name)).toEqual(['ERR_I2C_BUS_STUCK'])
    expect(log.clear({ ref: 'i2c_bus:1' })).toBe(1)
    expect(log.entries()).toEqual([])
  })

  it('keeps at most `capacity`: read entries go first', () => {
    const c = clock()
    const log = new ErrorLog(interpreter, { capacity: 2, now: c.now })
    log.addBytes(pinError(1, 1))
    c.tick(1)
    log.addBytes(pinError(2, 2))
    log.markRead({ ids: [log.entries()[0]!.id] }) // the newest, pin 2, is read
    c.tick(1)
    log.addBytes(pinError(3, 3))
    expect(log.entries().map((e) => e.report.cause!.fields[1]!.value)).toEqual([3, 1])
  })

  it("takes the link's errors stream and a failed command's record, and tells who listens", async () => {
    const board = new FakeBoard()
    const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
    await link.connect(board.device)
    const log = new ErrorLog(interpreter)
    const changes: number[] = []
    log.onChange((entries) => changes.push(entries.length))
    log.attach(link)
    board.notify(Uint8Array.from([defs.streams.errors, ...pinError(4)]), defs.gatt.logs)
    await until(() => log.entries().length === 1)
    const failed = new CommandError('refused', 'device-error', { record: interpreter.read(record([['ERR_DEV_SUSPENDED', { id: 3 }]]))!.record })
    expect(log.addCommandError(failed)!.report.message).toBe('Left motor is suspended')
    expect(log.addCommandError(new CommandError('late', 'timeout'))).toBeUndefined()
    expect(changes).toEqual([1, 2])
    log.close()
    await link.close()
  })
})

// the report is plain data an app can keep
const _typed: (r: ErrorReport) => string = (r) => r.message
void _typed
