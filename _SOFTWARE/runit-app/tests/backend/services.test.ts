import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import {
  bundledServiceCatalog, createDeviceCall, createRemoteDeviceCall, decodeAnswer, decodeFields, deviceCall, deviceOp, encodeFields, PayloadError, PIN_REF_SIZE, rawWidth, readDeviceFrame,
  requestFrame, RunitLink, ServiceCatalog, serviceCall, Services, sidesOf,
} from '@backend'
import type { FieldValues, Layout } from '@backend'
import { bundledEnums } from '@backend'
import { answer, defs, FakeBoard, until } from './fakes'

const root = fileURLToPath(new URL('../../../runit-esp/', import.meta.url))
const generatedDir = fileURLToPath(new URL('../../generated', import.meta.url))
const catalog = bundledServiceCatalog()
const hex = (b: Uint8Array) => [...b].map((x) => x.toString(16).padStart(2, '0')).join('')

/* ---- the C: packed structs of the firmware headers, for holding the layouts against them ---- */

const walk = (dir: string): string[] =>
  readdirSync(dir).flatMap((name) => {
    const path = join(dir, name)
    return statSync(path).isDirectory() ? walk(path) : path.endsWith('.h') ? [path] : []
  })

const C_WIDTH: Record<string, number> = { uint8_t: 1, int8_t: 1, char: 1, bool: 1, uint16_t: 2, int16_t: 2, uint32_t: 4, int32_t: 4, float: 4, uint64_t: 8, int64_t: 8, double: 8 }

interface CStruct {
  readonly size: number
  /** Each member's offset and bytes. */
  readonly members: ReadonlyMap<string, { readonly offset: number; readonly size: number }>
}

/** Every `typedef struct __attribute__((packed)) { … } name;` of runit-esp/components/, with its members' offsets (undefined: a member of unknown type). */
const packedStructs = (): Map<string, CStruct | undefined> => {
  const bodies = new Map<string, string>()
  for (const file of walk(join(root, 'components'))) {
    const text = readFileSync(file, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '')
    for (const m of text.matchAll(/typedef\s+struct\s+__attribute__\s*\(\(\s*packed\s*\)\)\s*\w*\s*\{([^{}]*)\}\s*(\w+)\s*;/g)) bodies.set(m[2]!, m[1]!)
  }
  const done = new Map<string, CStruct | undefined>()
  const size = (name: string): CStruct | undefined => {
    if (done.has(name)) return done.get(name)
    const body = bodies.get(name)
    if (body === undefined) return undefined
    done.set(name, undefined)
    const members = new Map<string, { offset: number; size: number }>()
    let at = 0
    for (const decl of body.split(';').map((d) => d.trim()).filter(Boolean)) {
      const m = decl.match(/^(?:const\s+)?(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?$/)
      if (!m) return undefined
      const width = C_WIDTH[m[1]!] ?? size(m[1]!)?.size
      if (width === undefined) return undefined
      members.set(m[2]!, { offset: at, size: width * Number(m[3] ?? 1) })
      at += width * Number(m[3] ?? 1)
    }
    const struct = { size: at, members }
    done.set(name, struct)
    return struct
  }
  for (const name of bodies.keys()) size(name)
  return done
}

const layouts = (): { where: string; layout: Layout }[] =>
  catalog.entries.flatMap((entry) => [
    ...(entry.config ? [{ where: `${entry.id} config`, layout: entry.config }] : []),
    ...[...entry.commands.values()].flatMap((c) =>
      sidesOf(c).flatMap((s) => [
        ...(c[s]!.args ? [{ where: `${entry.id} ${c.id} ${s} args`, layout: c[s]!.args! }] : []),
        ...(c[s]!.returns?.kind === 'struct' ? [{ where: `${entry.id} ${c.id} ${s} returns`, layout: (c[s]!.returns as { layout: Layout }).layout }] : []),
      ]),
    ),
  ])

/** A value for every field of a layout: its default, else its minimum or zero (bytes of its size, an empty string, a list for an array). */
const zeros = (layout: Layout): FieldValues =>
  Object.fromEntries(
    layout.fields.map((f) => {
      if (f.default !== undefined) return [f.id, f.default as never]
      if (f.pinRef) return [f.id, f.size === PIN_REF_SIZE ? null : Array.from({ length: f.size! / PIN_REF_SIZE }, () => null)]
      if (f.rawType === 'bytes') return [f.id, new Uint8Array(f.size ?? 0)]
      if (f.rawType === 'string') return [f.id, '']
      const value = f.min !== undefined && f.min > 0 ? f.min : 0
      const count = (f.size ?? rawWidth(f.rawType)!) / rawWidth(f.rawType)!
      return [f.id, count === 1 ? value : Array.from({ length: count }, () => value)]
    }),
  )

describe('the catalog', () => {
  it('holds together: no field overlaps, every key and contract id is published, every op is one command', () => {
    expect(catalog.problems).toEqual([])
  })

  it('has every descriptor of generated/ (nothing listed by hand)', () => {
    const count = (dir: string, prefix: string) => readdirSync(join(generatedDir, dir)).filter((f) => f.startsWith(prefix) && f.endsWith('.json')).length
    expect(catalog.entries.length).toBe(count('services', 'contract_') + count('contracts', 'contract_') + count('devices', 'device_'))
  })

  it('every layout of a packed C struct is that struct: its size, and every field inside the member of its name', () => {
    const structs = packedStructs()
    const wrong: string[] = []
    let checked = 0
    for (const { where, layout } of layouts()) {
      const c = layout.struct ? structs.get(layout.struct) : undefined
      if (!c) continue
      checked++
      if (!layout.variable && layout.fixed !== c.size) wrong.push(`${where} (${layout.struct}): ${layout.fixed} B, the C is ${c.size} B`)
      for (const f of layout.fields) {
        // a field may be a part of a member (the pin byte of a sys_io_pin_t member)
        const member = c.members.get(f.id)
        if (member && (f.offset < member.offset || f.offset + (f.size ?? 0) > member.offset + member.size)) wrong.push(`${where} ${f.id}: ${f.offset}+${f.size}, the C member is ${member.offset}+${member.size}`)
      }
    }
    expect(wrong).toEqual([])
    expect(checked).toBeGreaterThan(20)
  })

  it('reports a descriptor that does not hold together, and keeps the rest', () => {
    const bad = new ServiceCatalog(
      [
        { source: 'a.json', json: { id: 'A', router_key: 250, commands: [{ id: 'X', op: 1, write: { args: { fields: [{ id: 'p', type: 'integer', raw_type: 'u16', offset: 0 }, { id: 'q', type: 'integer', raw_type: 'u8', offset: 1 }] } } }, { id: 'Y', op: 1, read: {} }, { id: 'W', op: 3 }] } },
        { source: 'b.json', json: { id: 'B', contract_id: 1, commands: [{ id: 'Z', op: 2, write: {} }] } },
        { source: 'c.json', json: { id: 'nothing' } },
      ],
      bundledEnums(),
    )
    expect(bad.problems.join('\n')).toMatch(/router key 250/)
    expect(bad.problems.join('\n')).toMatch(/q \(1\.\.1\) overlaps p \(0\.\.1\)/)
    expect(bad.problems.join('\n')).toMatch(/op 1 is both X and Y/)
    expect(bad.problems.join('\n')).toMatch(/command W: neither a read nor a write/)
    expect(bad.problems.join('\n')).toMatch(/c\.json: neither/)
    expect(bad.contract('B').commands.has('Z')).toBe(true)
  })
})

describe('requests', () => {
  it('a service command: [key][dir][op][args], laid out by the descriptor', () => {
    const call = serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: 500 }, 'write')
    expect(call.request.keys).toEqual([defs.keys.sys, defs.dir.write, catalog.service('SYS').commands.get('DEADMAN')!.op])
    expect(hex(call.request.payload!)).toBe('f4010000')
    expect(defs.dir).toEqual({ read: 0, write: 1 }) // cmd_dir_e, cmd.h
  })

  it('a command with a read and a write is told which; one with a single side runs it', () => {
    const read = serviceCall(catalog, 'SYS', 'DEADMAN', undefined, 'read')
    expect(read.request.keys).toEqual([defs.keys.sys, defs.dir.read, catalog.service('SYS').commands.get('DEADMAN')!.op])
    expect(read.request.payload!.length).toBe(0)
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: 500 })).toThrow(/say which one/)
    expect(serviceCall(catalog, 'SYS', 'PING').request.keys[1]).toBe(defs.dir.read)
    expect(serviceCall(catalog, 'SYS', 'STOP').request.keys[1]).toBe(defs.dir.write)
    expect(() => serviceCall(catalog, 'SYS', 'PING', undefined, 'write')).toThrow(/no write side/)
  })

  it('a mirror: the read takes the key and answers what the write takes', () => {
    const level = catalog.contract('IO').commands.get('LEVEL')!
    expect(level.read!.args!.fields.map((f) => f.id)).toEqual(['pin'])
    expect(level.read!.returns?.kind === 'struct' && level.read!.returns.layout.fields.map((f) => f.id)).toEqual(level.write!.args!.fields.map((f) => f.id))
    const read = deviceCall(defs, catalog, 1, 'IO', 'LEVEL', { pin: 22 }, 'read')
    const op = deviceOp(catalog, catalog.contract('IO'), level)
    expect(hex(requestFrame(read.request))).toBe(hex(Uint8Array.of(defs.keys.sys_device!, defs.dir.read, 1, op & 0xff, op >> 8, 22)))
    expect(decodeAnswer(read, Uint8Array.of(22, 1))).toEqual({ pin: 22, level: 1 })
  })

  it('a side whose arguments are rows takes a list (the response matrix, one row per target)', () => {
    const matrix = catalog.service('POWER_MANAGER').commands.get('MATRIX')!
    const layout = matrix.write!.args!
    expect(layout.array).toBe(true)
    const row = { target: 0xffff, responses: Array.from({ length: layout.fields[1]!.size! }, () => 0) }
    const call = serviceCall(catalog, 'POWER_MANAGER', 'MATRIX', [row, { ...row, target: 5 }], 'write')
    expect(call.request.payload!.length).toBe(2 * layout.fixed)
    expect(hex(call.request.payload!.subarray(layout.fixed, layout.fixed + 2))).toBe('0500')
    expect(() => serviceCall(catalog, 'POWER_MANAGER', 'MATRIX', row, 'write')).toThrow(/takes rows/)
  })

  it('an urgent command carries the flag the descriptor gives it', () => {
    const urgent = [...catalog.service('SYS').commands.values()].filter((c) => c.urgent)
    expect(urgent.length).toBeGreaterThan(0)
    for (const c of urgent) expect(serviceCall(catalog, 'SYS', c.id).request.urgent).toBe(true)
    expect(serviceCall(catalog, 'SYS', 'PING').request.urgent).toBeUndefined()
  })

  it('a device command is [device key][dir][id u8][contract << 8 | op u16][args], and a frame reads back into it', () => {
    const call = deviceCall(defs, catalog, 1, 'IO', 'LEVEL', { pin: 22, level: 'High' }, 'write')
    const io = catalog.contract('IO')
    expect(hex(requestFrame(call.request))).toBe(hex(Uint8Array.of(defs.keys.sys_device!, defs.dir.write, 1, 0x00, io.contractId!, 22, 1)))
    const back = readDeviceFrame(defs, catalog, requestFrame(call.request))
    expect(back).toMatchObject({ device: 1, side: 'write', args: { pin: 22, level: 1 } })
    expect(back?.entry.id).toBe('IO')
    expect(back?.command.id).toBe('LEVEL')
  })

  it("a device type's own command is in the own contract", () => {
    const call = deviceCall(defs, catalog, 9, 'dc_motor', 'speed', { speed_x10: -90 }, 'write')
    expect(hex(call.request.payload!)).toBe('09' + hex(Uint8Array.of(catalog.device('dc_motor').commands.get('speed')!.op, catalog.ownContractId!)) + '7cfc')
    const back = readDeviceFrame(defs, catalog, requestFrame(call.request), () => catalog.device('dc_motor'))
    expect(back).toMatchObject({ device: 9, side: 'write', args: { speed_x10: -90 } })
  })

  it('CREATE is a write of [type u16][flags u8][config] (SYS_DEVICE.MD: pca9685 under ID 3), and reads back', () => {
    const pca = catalog.device('pca9685')
    const call = createDeviceCall(defs, catalog, 3, 'pca9685', zeros(pca.config!))
    expect(call.request.keys).toEqual([defs.keys.sys_device, defs.dir.write])
    expect(hex(call.request.payload!).slice(0, 12)).toBe(`03${hex(Uint8Array.of(0, catalog.lifecycleContractId!))}${hex(Uint8Array.of(pca.typeId! & 0xff, pca.typeId! >> 8))}00`)
    expect(call.request.payload!.length).toBe(3 + 3 + pca.config!.fixed)
    const system = createDeviceCall(defs, catalog, 3, 'pca9685', zeros(pca.config!), true)
    expect(hex(system.request.payload!).slice(10, 12)).toBe('01')
    const back = readDeviceFrame(defs, catalog, requestFrame(system.request))
    expect(back?.create).toMatchObject({ system: true, type: { id: 'pca9685' } })
  })

  it('CREATE_REMOTE says how its calls go: wireless by default, wired when asked', () => {
    const call = createRemoteDeviceCall(defs, catalog, 20, 'esp32s3_gpio', 2, 0)
    const wired = createRemoteDeviceCall(defs, catalog, 20, 'esp32s3_gpio', 2, 0, 'wired')
    // [id u8][op u16][type u16][board][remote id][flags]
    expect(call.request.payload!.at(-1)).toBe(catalog.remoteFlags.wireless)
    expect(wired.request.payload!.at(-1)).toBe(catalog.remoteFlags.wired)
    expect(catalog.remoteFlags.wired).not.toBe(catalog.remoteFlags.wireless)
  })

  it('a pin reference is a sys_io_pin_t: { device, pin }, null for an unused pin (SYS_IO_PIN_NONE), a list for several', () => {
    const c = packedStructs().get('sys_io_pin_t')!
    expect(c.size).toBe(PIN_REF_SIZE)
    expect([...c.members.entries()]).toEqual([['device', { offset: 0, size: 1 }], ['pin', { offset: 1, size: 1 }]])
    expect(catalog.pinNone).toEqual({ device: 0xff, pin: 0xff }) // SYS_IO_PIN_NONE, sys_io.h

    const i2c = catalog.device('esp32s3_i2c').config!
    const sda = i2c.fields.find((f) => f.id === 'sda')!
    const values = { ...zeros(i2c), sda: { device: 0x02, pin: 7 } }
    const payload = encodeFields(i2c, values)
    expect(hex(payload.subarray(sda.offset, sda.offset + 2))).toBe('0207') // the device, then the pin
    expect(decodeFields(i2c, payload).sda).toEqual({ device: 2, pin: 7 })
    expect(() => encodeFields(i2c, { ...values, sda: Uint8Array.of(2, 7) })).toThrow(PayloadError)
    expect(() => encodeFields(i2c, { ...values, sda: { device: 0x100, pin: 0 } })).toThrow(/not a u8/)

    // an optional pin left out is no pin, not device 0 / pin 0 (a real pin)
    const tps = catalog.device('tps55289').config!
    const en = tps.fields.find((f) => f.id === 'en_pin')!
    expect(en.optional).toBe(true)
    const { en_pin: _, ...rest } = zeros(tps)
    const none = encodeFields(tps, rest)
    expect(hex(none.subarray(en.offset, en.offset + 2))).toBe('ffff')
    expect(decodeFields(tps, none).en_pin).toBeNull()
  })

  it('every command of every descriptor encodes from its defaults and zeros', () => {
    for (const entry of catalog.entries) {
      for (const c of entry.commands.values()) for (const s of sidesOf(c)) if (c[s]!.args) expect(() => encodeFields(c[s]!.args!, zeros(c[s]!.args!)), `${entry.id} ${c.id} ${s}`).not.toThrow()
      if (entry.config) expect(() => encodeFields(entry.config!, zeros(entry.config!)), `${entry.id} config`).not.toThrow()
    }
  })

  it('an enum field takes a member name', () => {
    const members = bundledEnums().sys_io_mode_e!.members
    const mode = deviceCall(defs, catalog, 0, 'IO', 'MODE', { pin: 4, mode: members[1]!.name })
    expect(mode.request.payload![4]).toBe(members[1]!.value)
  })

  it('refuses a missing field, a field that is not there, a value that does not fit, and arguments to a command without any', () => {
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN', {}, 'write')).toThrow(/timeout_ms is missing/)
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: 1, extra: 1 }, 'write')).toThrow(/No field extra/)
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: -1 }, 'write')).toThrow(PayloadError)
    expect(() => deviceCall(defs, catalog, 0, 'IO', 'LEVEL', { pin: 1, level: 'Middle' }, 'write')).toThrow(/'Middle' is not a value of level/)
    expect(() => serviceCall(catalog, 'SYS', 'PING', { x: 1 })).toThrow(/takes no arguments/)
    expect(() => serviceCall(catalog, 'NOPE', 'PING')).toThrow(/No service NOPE/)
  })
})

describe('answers', () => {
  it('a struct by field id, booleans as booleans', () => {
    expect(decodeAnswer(serviceCall(catalog, 'SYS', 'PING'), Uint8Array.of(0xd2, 0x04, 0, 0))).toEqual({ uptime_ms: 1234 })
    expect(decodeAnswer(serviceCall(catalog, 'SYS', 'DEADMAN', undefined, 'read'), Uint8Array.of(0xf4, 1, 0, 0))).toEqual({ timeout_ms: 500 })
    expect(decodeAnswer(serviceCall(catalog, 'SYS', 'DEADMAN', { timeout_ms: 500 }, 'write'), new Uint8Array())).toBeUndefined() // a write answers no data
  })

  it('an array field is a list both ways', () => {
    const matrix = catalog.service('POWER_MANAGER').commands.get('MATRIX')!
    const layout = matrix.write!.args!
    const field = layout.fields.find((f) => f.size! > rawWidth(f.rawType)!)!
    const count = field.size! / rawWidth(field.rawType)!
    const list = Array.from({ length: count }, (_, i) => i)
    const bytes = encodeFields(layout, { ...zeros(layout), [field.id]: list })
    expect(decodeFields(layout, bytes)[field.id]).toEqual(list)
    expect(() => encodeFields(layout, { ...zeros(layout), [field.id]: [1] })).toThrow(new RegExp(`takes ${count} values`))
  })

  it('a list of values and a list of structs', () => {
    // OPS: [op u16][sides u8] per op (sys_device.c), bit 0 a read side, bit 1 a write side
    expect(decodeAnswer(deviceCall(defs, catalog, 0, 'DEVICE', 'OPS'), Uint8Array.of(2, 0, 2, 7, 0, 3, 0, 1, 1))).toEqual([
      { op: 2, sides: 2 },
      { op: 7, sides: 3 },
      { op: 0x100, sides: 1 },
    ])
    const matrix = catalog.service('POWER_MANAGER').commands.get('MATRIX')!
    const returns = matrix.read?.returns
    if (returns?.kind !== 'struct') throw new Error('POWER_MANAGER MATRIX is a struct list')
    const one = returns.layout.fixed
    const decoded = decodeAnswer(serviceCall(catalog, 'POWER_MANAGER', 'MATRIX', undefined, 'read'), new Uint8Array(one * 2)) as unknown[]
    expect(decoded).toHaveLength(2)
    expect(() => decodeAnswer(serviceCall(catalog, 'POWER_MANAGER', 'MATRIX', undefined, 'read'), new Uint8Array(one + 1))).toThrow(/whole elements/)
  })

  it('a mirror of a device type: the read takes the key and answers what the write takes', () => {
    const write = deviceCall(defs, catalog, 4, 'dc_motor', 'speed', { speed_x10: 50 }, 'write')
    const read = deviceCall(defs, catalog, 4, 'dc_motor', 'speed', undefined, 'read')
    expect(read.request.payload!.length).toBe(3)
    expect(decodeAnswer(read, write.request.payload!.subarray(3))).toEqual({ speed_x10: 50 })
  })

  it('a value that leads the answer is read from the front; bytes stay bytes', () => {
    expect(decodeAnswer(serviceCall(catalog, 'DATA_MATRIX', 'LIST'), Uint8Array.of(3, 9, 9))).toBeDefined()
    expect(decodeAnswer(serviceCall(catalog, 'BLE', 'LIST'), Uint8Array.of(1, 2))).toEqual(Uint8Array.of(1, 2))
  })
})

describe('over the link', () => {
  it('sends the request and decodes the answer', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [0x10, 0x27, 0, 0]))
    const link = new RunitLink(board)
    await link.connect(board.device)
    const services = new Services(link)
    const out = await services.call('SYS', 'PING')
    expect(out.value).toEqual({ uptime_ms: 10_000 })
    expect([...board.requests[0]!.subarray(2)]).toEqual([defs.keys.sys, defs.dir.read, catalog.service('SYS').commands.get('PING')!.op])
    await services.call('SYS', 'STOP')
    await until(() => board.requests.length === 2)
    expect(board.requests[1]![1]).toBe(defs.flags.urgent)
    await link.close()
  })
})
