import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import {
  bundledServiceCatalog, createDeviceCall, decodeAnswer, decodeFields, deviceCall, deviceFrame, encodeFields, PayloadError, PIN_REF_SIZE, rawWidth, requestFrame, RunitLink,
  ServiceCatalog, serviceCall, Services,
} from '@backend'
import type { FieldValues, Layout } from '@backend'
import { generatedEnums } from '@engine/generated'
import { answer, defs, FakeBoard, until } from './fakes'

const root = fileURLToPath(new URL('../../../', import.meta.url))
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

/** Every `typedef struct __attribute__((packed)) { … } name;` of components/, with its members' offsets (undefined: a member of unknown type). */
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
    ...[...entry.commands.values()].flatMap((c) => [
      ...(c.args ? [{ where: `${entry.id} ${c.id} args`, layout: c.args }] : []),
      ...(c.returns?.kind === 'struct' ? [{ where: `${entry.id} ${c.id} returns`, layout: c.returns.layout }] : []),
    ]),
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
    const count = (dir: string, prefix: string) => readdirSync(join(root, 'generated', dir)).filter((f) => f.startsWith(prefix) && f.endsWith('.json')).length
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
        { source: 'a.json', json: { id: 'A', router_key: 250, commands: [{ id: 'X', op: 1, args: { fields: [{ id: 'p', type: 'integer', raw_type: 'u16', offset: 0 }, { id: 'q', type: 'integer', raw_type: 'u8', offset: 1 }] } }, { id: 'Y', op: 1 }] } },
        { source: 'b.json', json: { id: 'B', contract_id: 1, commands: [{ id: 'Z', op: 2 }] } },
        { source: 'c.json', json: { id: 'nothing' } },
      ],
      generatedEnums,
    )
    expect(bad.problems.join('\n')).toMatch(/router key 250/)
    expect(bad.problems.join('\n')).toMatch(/q \(1\.\.1\) overlaps p \(0\.\.1\)/)
    expect(bad.problems.join('\n')).toMatch(/op 1 is both X and Y/)
    expect(bad.problems.join('\n')).toMatch(/c\.json: neither/)
    expect(bad.contract('B').commands.has('Z')).toBe(true)
  })
})

describe('requests', () => {
  it('a service command: [key][op][args], laid out by the descriptor', () => {
    const call = serviceCall(catalog, 'SYS', 'DEADMAN_SET', { timeout_ms: 500, on_link_lost: true })
    expect(call.request.keys).toEqual([defs.keys.sys, catalog.service('SYS').commands.get('DEADMAN_SET')!.op])
    expect(hex(call.request.payload!)).toBe('f401000001')
  })

  it('an urgent command carries the flag the descriptor gives it', () => {
    const urgent = [...catalog.service('SYS').commands.values()].filter((c) => c.urgent)
    expect(urgent.length).toBeGreaterThan(0)
    for (const c of urgent) expect(serviceCall(catalog, 'SYS', c.id).request.urgent).toBe(true)
    expect(serviceCall(catalog, 'SYS', 'PING').request.urgent).toBeUndefined()
  })

  it('a device command is the frame the board ran (the project fixture): [device key][id][contract << 8 | op][args]', () => {
    const call = deviceCall(defs, catalog, 1, 'IO', 'SET_LEVEL', { pin: 22, level: 'High' })
    expect(hex(requestFrame(call.request))).toBe(hex(deviceFrame(defs, 1, 0x0100, Uint8Array.of(22, 1))))
  })

  it("a device type's own command is in the own contract", () => {
    const call = deviceCall(defs, catalog, 9, 'servo', 'set', { value_x10: -900 })
    expect(hex(call.request.payload!)).toBe('0900' + hex(Uint8Array.of(catalog.device('servo').commands.get('set')!.op, catalog.ownContractId!)) + '7cfc')
  })

  it('CREATE is [type u16][config] (SYS_DEVICE.MD: pca9685 under ID 3)', () => {
    const pca = catalog.device('pca9685')
    const call = createDeviceCall(defs, catalog, 3, 'pca9685', zeros(pca.config!))
    expect(call.request.keys).toEqual([defs.keys.device])
    expect(hex(call.request.payload!).slice(0, 12)).toBe('030000004100')
    expect(call.request.payload!.length).toBe(6 + pca.config!.fixed)
  })

  it('a pin reference is a sys_io_pin_t: { device, pin }, null for an unused pin (SYS_IO_PIN_NONE), a list for several', () => {
    const c = packedStructs().get('sys_io_pin_t')!
    expect(c.size).toBe(PIN_REF_SIZE)
    expect([...c.members.entries()]).toEqual([['device', { offset: 0, size: 2 }], ['pin', { offset: 2, size: 1 }]])
    expect(catalog.pinNone).toEqual({ device: 0xffff, pin: 0xff }) // SYS_IO_PIN_NONE, sys_io.h

    const servo = catalog.device('servo').config!
    const pwm = servo.fields.find((f) => f.id === 'pwm_pin')!
    const values = { ...zeros(servo), pwm_pin: { device: 0x0102, pin: 7 } }
    const payload = encodeFields(servo, values)
    expect(hex(payload.subarray(pwm.offset, pwm.offset + 3))).toBe('020107') // u16 little endian, then the pin
    expect(decodeFields(servo, payload).pwm_pin).toEqual({ device: 0x0102, pin: 7 })
    expect(() => encodeFields(servo, { ...values, pwm_pin: Uint8Array.of(2, 1, 7) })).toThrow(PayloadError)
    expect(() => encodeFields(servo, { ...values, pwm_pin: { device: 0x10000, pin: 0 } })).toThrow(/not a u16/)

    // an optional pin left out is no pin, not device 0 / pin 0 (a real pin)
    const tps = catalog.device('tps55289').config!
    const en = tps.fields.find((f) => f.id === 'en_pin')!
    expect(en.optional).toBe(true)
    const { en_pin: _, ...rest } = zeros(tps)
    const none = encodeFields(tps, rest)
    expect(hex(none.subarray(en.offset, en.offset + 3))).toBe('ffffff')
    expect(decodeFields(tps, none).en_pin).toBeNull()

    // drv8962: 15 pins in one field
    const drv = catalog.device('drv8962').config!
    const pins = drv.fields.find((f) => f.pinRef && f.size! > PIN_REF_SIZE)!
    const list = Array.from({ length: pins.size! / PIN_REF_SIZE }, (_, i) => (i === 1 ? { device: 4, pin: 9 } : null))
    const decoded = decodeFields(drv, encodeFields(drv, { ...zeros(drv), [pins.id]: list }))[pins.id]
    expect(decoded).toEqual(list)
    expect(() => encodeFields(drv, { ...zeros(drv), [pins.id]: [null] })).toThrow(/takes 15 pins/)
  })

  it('every command of every descriptor encodes from its defaults and zeros', () => {
    for (const entry of catalog.entries) {
      for (const c of entry.commands.values()) if (c.args) expect(() => encodeFields(c.args!, zeros(c.args!)), `${entry.id} ${c.id}`).not.toThrow()
      if (entry.config) expect(() => encodeFields(entry.config!, zeros(entry.config!)), `${entry.id} config`).not.toThrow()
    }
  })

  it('an enum field takes a member name, a choice field its label; a string ends in one NUL', () => {
    const mode = deviceCall(defs, catalog, 0, 'IO', 'SET_MODE', { pin: 4, mode: generatedEnums.sys_io_mode_e!.members[1]!.name })
    expect(mode.request.payload![5]).toBe(generatedEnums.sys_io_mode_e!.members[1]!.value)
    const create = serviceCall(catalog, 'CONNECTOR', 'CREATE', { id: 4, stream: 0x20, name: 'app' })
    expect(hex(create.request.payload!)).toBe('0420' + '61707000')
  })

  it('refuses a missing field, a field that is not there, a value that does not fit, and arguments to a command without any', () => {
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN_SET', { timeout_ms: 1 })).toThrow(/on_link_lost is missing/)
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN_SET', { timeout_ms: 1, on_link_lost: 0, extra: 1 })).toThrow(/No field extra/)
    expect(() => serviceCall(catalog, 'SYS', 'DEADMAN_SET', { timeout_ms: -1, on_link_lost: 0 })).toThrow(PayloadError)
    expect(() => deviceCall(defs, catalog, 0, 'IO', 'SET_LEVEL', { pin: 1, level: 'Middle' })).toThrow(/'Middle' is not a value of level/)
    expect(() => serviceCall(catalog, 'SYS', 'PING', { x: 1 })).toThrow(/takes no arguments/)
    expect(() => serviceCall(catalog, 'NOPE', 'PING')).toThrow(/No service NOPE/)
  })
})

describe('answers', () => {
  it('a struct by field id, booleans as booleans', () => {
    expect(decodeAnswer(serviceCall(catalog, 'SYS', 'PING'), Uint8Array.of(0xd2, 0x04, 0, 0))).toEqual({ uptime_ms: 1234 })
    expect(decodeAnswer(serviceCall(catalog, 'SYS', 'DEADMAN_GET'), Uint8Array.of(0xf4, 1, 0, 0, 1))).toEqual({ timeout_ms: 500, on_link_lost: true })
  })

  it('VM STATUS: the run of the program, from the bytes the board answers (vm_wire_status_t, 26 bytes)', () => {
    // running, loaded, ready, no fault, slow-down 1, 1096 passes, last pass 19 us, 12 B of arena, parked before block 3 (a board answer, `next_block` added at the end)
    const bytes = Uint8Array.of(1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0x48, 0x04, 0, 0, 19, 0, 0, 0, 12, 0, 0, 0, 3, 0)
    const value = decodeAnswer(serviceCall(catalog, 'VM', 'STATUS'), bytes) as Record<string, unknown>
    expect(value).toMatchObject({ load_state: 1, ready: true, fault_latched: false, slowdown: 1, pass_count: 1096, last_pass_us: 19, arena_used: 12, next_block: 3 })
    expect(value.mode).toBe(1)
  })

  it('an array field (u32 of 16 bytes) is a list both ways', () => {
    const set = deviceCall(defs, catalog, 2, 'DEVICE', 'SET_ERROR_POLICY', { importance: 3, actions: [10, 0, 0, 0x01000001] })
    expect(hex(set.request.payload!.subarray(4))).toBe('03' + '0a000000' + '00000000' + '00000000' + '01000001')
    expect(decodeAnswer(deviceCall(defs, catalog, 2, 'DEVICE', 'GET_ERROR_POLICY'), set.request.payload!.subarray(4))).toEqual({ importance: 3, actions: [10, 0, 0, 0x01000001] })
    expect(() => deviceCall(defs, catalog, 2, 'DEVICE', 'SET_ERROR_POLICY', { importance: 3, actions: [1] })).toThrow(/takes 4 values/)
  })

  it('a list of values and a list of structs', () => {
    expect(decodeAnswer(deviceCall(defs, catalog, 0, 'DEVICE', 'OPS'), Uint8Array.of(2, 0, 3, 0, 0, 1))).toEqual([2, 3, 0x100])
    const info = catalog.service('I2C').commands.get('INFO')!
    if (info.returns?.kind !== 'struct') throw new Error('I2C INFO is a struct list')
    const one = info.returns.layout.fixed
    const decoded = decodeAnswer(serviceCall(catalog, 'I2C', 'INFO'), new Uint8Array(one * 2)) as unknown[]
    expect(decoded).toHaveLength(2)
    expect(() => decodeAnswer(serviceCall(catalog, 'I2C', 'INFO'), new Uint8Array(one + 1))).toThrow(/whole elements/)
  })

  it('a value that leads the answer is read from the front; bytes stay bytes', () => {
    expect(decodeAnswer(serviceCall(catalog, 'CONNECTOR', 'INFO'), Uint8Array.of(3, 9, 9))).toBe(3)
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
    expect([...board.requests[0]!.subarray(2)]).toEqual([defs.keys.sys, catalog.service('SYS').commands.get('PING')!.op])
    await services.call('SYS', 'STOP')
    await until(() => board.requests.length === 2)
    expect(board.requests[1]![1]).toBe(defs.flags.urgent)
    await link.close()
  })
})
