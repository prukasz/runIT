import { readdirSync, readFileSync, statSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { Catalog, DEVICE_STATE, shapeOf } from '@engine/catalog'
import { loadGeneratedInput } from '@engine/generated'

const catalog = Catalog.fromGenerated(loadGeneratedInput())

/* The firmware's own source: what the descriptors and the engine's tables are held against. */
const vmDir = fileURLToPath(new URL('../../../../components/vm/', import.meta.url))
const read = (path: string) => readFileSync(vmDir + path, 'utf8')
const filesUnder = (dir: string, ext: string): string[] =>
  readdirSync(vmDir + dir).flatMap((name) => {
    const rel = `${dir}/${name}`
    return statSync(vmDir + rel).isDirectory() ? filesUnder(rel, ext) : rel.endsWith(ext) ? [rel] : []
  })

describe('the catalog from generated/', () => {
  it('has all 22 blocks and needs no assumption', () => {
    expect(catalog.blocks).toHaveLength(22)
    expect(catalog.warnings).toEqual([])
  })

  it('orders the palette by category, then order', () => {
    const groups = [...catalog.categories().keys()]
    expect(groups).toEqual(['flow', 'logic', 'data', 'time', 'device', 'system'])
    expect(catalog.categories().get('flow')?.map((b) => b.id)).toEqual(['if', 'switch', 'for'])
  })

  it('looks a block up by id and by wire number', () => {
    expect(catalog.get('if')?.wireId).toBe(3)
    expect(catalog.byWireId(32)?.id).toBe('io')
    expect(catalog.get('nope')).toBeUndefined()
    expect(() => catalog.require('nope')).toThrow(/no block 'nope'/)
  })
})

describe('held against the firmware source', () => {
  it('wire ids equal vm_block_ids.h', () => {
    const ids = new Map([...read('vm_core/block/vm_block_ids.h').matchAll(/^#define\s+VM_BLK_([A-Z0-9_]+)\s+(\d+)/gm)].map((m) => [m[1]!.toLowerCase(), Number(m[2])]))
    for (const block of catalog.blocks) expect(ids.get(block.id), `VM_BLK_${block.id.toUpperCase()}`).toBe(block.wireId)
    // every block of the header with a descriptor, and no descriptor without a block
    const described = new Set(catalog.blocks.map((b) => b.id))
    for (const id of ids.keys()) if (id !== 'none') expect(described.has(id), `${id} has no descriptor`).toBe(true)
  })

  /** `VM_BLOCK_TYPE_DEFINE(VM_BLK_X, .min_in = 0, ...)` of every block file, as numbers. */
  const palette = (() => {
    const symbols: Record<string, number> = { CONFIG_VM_BLOCK_MAX_IN: 16, CONFIG_VM_BLOCK_MAX_OUT: 16, CONFIG_VM_BLOCK_MAX_EN: 16, VM_DEV_FIELDS_MAX: 4 }
    const out = new Map<string, Record<string, number>>()
    for (const file of filesUnder('vm_blocks', '.c')) {
      for (const m of read(file).matchAll(/VM_BLOCK_TYPE_DEFINE\(\s*VM_BLK_([A-Z0-9_]+)\s*,([\s\S]*?)\);/g)) {
        const entry: Record<string, number> = {}
        for (const kv of m[2]!.matchAll(/\.(min_in|max_in|min_q|max_q|min_en|required_in)\s*=\s*([A-Za-z0-9_]+)/g)) {
          const raw = kv[2]!
          entry[kv[1]!] = raw in symbols ? symbols[raw]! : Number(raw.replace(/u$/i, '')) // `0x1u` is a C unsigned literal
        }
        out.set(m[1]!.toLowerCase(), entry)
      }
    }
    return out
  })()

  it('read a palette entry for every block', () => {
    for (const block of catalog.blocks) expect(palette.has(block.id), block.id).toBe(true)
  })

  it('core blocks have the pin shape of their VM_BLOCK_TYPE_DEFINE', () => {
    for (const block of catalog.blocks.filter((b) => b.integration === 'core')) {
      const c = palette.get(block.id)!
      const s = shapeOf(block)
      expect(
        { minIn: s.minIn, maxIn: s.maxIn, minQ: s.minQ, maxQ: s.maxQ, minEn: s.minEn, requiredIn: s.requiredIn },
        block.id,
      ).toEqual({ minIn: c.min_in ?? 0, maxIn: c.max_in ?? 0, minQ: c.min_q ?? 0, maxQ: c.max_q ?? 0, minEn: c.min_en ?? 0, requiredIn: c.required_in ?? 0 })
    }
  })

  it('device blocks never exceed the pins of their VM_BLOCK_TYPE_DEFINE, in any variant', () => {
    for (const block of catalog.blocks.filter((b) => b.integration !== 'core')) {
      const c = palette.get(block.id)!
      for (const variant of block.variants.filter((v) => !v.unsupported)) {
        const s = shapeOf(block, variant.id)
        expect(s.maxIn, `${block.id}.${variant.id} inputs`).toBeLessThanOrEqual(c.max_in ?? 0)
        expect(s.maxQ, `${block.id}.${variant.id} outputs`).toBeLessThanOrEqual(c.max_q ?? 0)
      }
    }
  })

  it('state_len of every descriptor equals the .state_len of its VM_BLOCK_TYPE_DEFINE', () => {
    // sizeof(T) of every `_Static_assert(... sizeof(T) == N ...)` of the VM sources
    const sizes = new Map<string, number>()
    const sources = filesUnder('.', '.h').concat(filesUnder('.', '.c'))
    for (const file of sources) {
      for (const [, condition] of read(file).matchAll(/_Static_assert\s*\(([\s\S]*?),\s*"/g)) {
        for (const [, type, size] of condition!.matchAll(/sizeof\s*\(\s*(\w+)\s*\)\s*==\s*(\d+)/g)) sizes.set(type!, Number(size))
      }
    }
    const lengths = new Map<string, number>()
    for (const file of filesUnder('vm_blocks', '.c')) {
      for (const [, macro, body] of read(file).matchAll(/VM_BLOCK_TYPE_DEFINE\s*\(\s*(VM_BLK_\w+)\s*,([\s\S]*?)\);/g)) {
        const found = /\.state_len\s*=\s*(?:sizeof\s*\(\s*(\w+)\s*\)|(\d+))/.exec(body!)
        const length = found ? (found[2] ? Number(found[2]) : sizes.get(found[1]!)) : 0
        expect(length, `${macro} .state_len`).toBeDefined()
        lengths.set(macro!.replace('VM_BLK_', '').toLowerCase(), length!)
      }
    }
    for (const block of catalog.blocks) expect(block.stateLength, `${block.id} state_len`).toBe(lengths.get(block.id))
    expect(lengths.size).toBe(catalog.blocks.length)
  })

  it('the device block state is laid out as vm_block_device_data_t is', () => {
    const header = read('vm_blocks/device-contract/vm_block_device.h')
    const fields = Number(/#define\s+VM_DEV_FIELDS_MAX\s+(\d+)/.exec(header)![1])
    const body = /typedef struct[^{}]*\{([^{}]*)\}\s*vm_block_device_data_t;/.exec(header)![1]!
    const width: Record<string, number> = { uint8_t: 1, uint16_t: 2, int32_t: 4 }
    const offsets = new Map<string, number>()
    let at = 0
    for (const [, type, name, count] of body.matchAll(/^\s*(uint8_t|uint16_t|int32_t)\s+(\w+)(?:\[(\w+)\])?\s*;/gm)) {
      const w = width[type!]!
      at = Math.ceil(at / w) * w
      offsets.set(name!, at)
      at += w * (count === undefined ? 1 : count === 'VM_DEV_FIELDS_MAX' ? fields : Number(count))
    }
    expect(Object.fromEntries(offsets)).toMatchObject({
      device_id: DEVICE_STATE.deviceId,
      op: DEVICE_STATE.op,
      idle_op: DEVICE_STATE.idleOp,
      flags: DEVICE_STATE.flags,
      k: DEVICE_STATE.k,
      idle_k: DEVICE_STATE.idleK,
      last: DEVICE_STATE.last,
    })
    expect(fields).toBe(DEVICE_STATE.fields)
    expect(at).toBe(56)
  })

  it('every field of a fixed-length state lies inside it', () => {
    for (const block of catalog.blocks) {
      if (block.stateVariable) continue
      for (const field of block.fields) {
        if (field.offset === undefined || field.size === undefined) continue
        expect(field.offset + field.size, `${block.id}.${field.id}`).toBeLessThanOrEqual(block.stateLength)
      }
    }
  })
})

describe('blocks as the UI and the compiler read them', () => {
  it('for: its three optional fields are its three inputs, in order', () => {
    const shape = shapeOf(catalog.require('for'))
    expect(shape.inputs.map((p) => (p.kind === 'pin' ? p.id : 'group'))).toEqual(['start', 'end', 'step'])
    expect(catalog.require('for').span).toEqual({ offset: 0 })
  })

  it('enum fields take their choices from the generated enums', () => {
    const unit = catalog.require('timer').fields.find((f) => f.id === 'time_base')
    expect(unit?.options?.length).toBeGreaterThan(1)
    expect(unit?.options?.every((o) => o.alias.length > 0)).toBe(true)
  })

  it('set: the source pin stands in for the constant while unwired', () => {
    const set = shapeOf(catalog.require('set'))
    const source = set.inputs[0]
    expect(source?.kind === 'pin' && source.overrides).toBe('value')
    const destination = set.inputs[1]
    expect(destination?.kind === 'pin' && destination.writes).toBe(true)
  })

  it('a contract variant resolves its command: op number, argument fields as inputs, returned value as output', () => {
    const io = catalog.require('io')
    const setLevel = io.variants.find((v) => v.id === 'set_level')!
    expect(setLevel.operation).toMatchObject({ source: 'contract', of: 'IO', command: 'SET_LEVEL', value: 0x0100 })
    expect(setLevel.fields.map((f) => f.id)).toEqual(['pin', 'level'])
    expect(setLevel.fields.every((f) => f.input === 'optional')).toBe(true)
    expect(setLevel.fields[0]?.required).toBe(true)
    expect(shapeOf(io, 'set_level').inputs).toHaveLength(2)

    expect(shapeOf(io, 'get_level').outputs).toHaveLength(1)
    expect(shapeOf(io, 'toggle').outputs).toHaveLength(0)
    expect(io.variants.find((v) => v.id === 'get_level')?.operation?.value).toBe(0x0102)
  })

  it('a command that returns a struct gives one output per field', () => {
    expect(shapeOf(catalog.require('power_usb_pd'), 'get_limits').outputs).toHaveLength(2)
    expect(shapeOf(catalog.require('drv8962'), 'get_fault').outputs.length).toBeGreaterThanOrEqual(2)
  })

  it('a custom block resolves its device type commands', () => {
    const drv = catalog.require('drv8962')
    expect(drv.variants.find((v) => v.id === 'get_fault')?.operation).toMatchObject({ source: 'device', of: 'drv8962', value: 0xff02 })
  })

  it('every variant of a device block resolves to an operation', () => {
    const unresolved = catalog.blocks.filter((b) => b.integration !== 'core').flatMap((b) => b.variants.filter((v) => v.unsupported !== undefined).map((v) => `${b.id}.${v.id}: ${v.unsupported}`))
    expect(unresolved).toEqual([])
    const servo = catalog.require('servo')
    expect(servo.variants.map((v) => [v.id, v.operation?.command])).toEqual([['position', 'set'], ['home', 'home'], ['release', 'release'], ['trim', 'set_trim']])
    // the position is the command's one argument, scaled to tenths of a degree: one input, no second field
    expect(servo.variants[0]!.fields.map((f) => [f.id, f.scale])).toEqual([['value_x10', 10]])
  })

  it('a variant the descriptor gives no operation for is marked unsupported, not guessed', () => {
    const input = loadGeneratedInput()
    const patched = {
      ...input,
      blocks: input.blocks.map((b) => (b.id !== 'servo' ? b : { ...b, variants: (b.variants as { id: string }[]).map((v) => (v.id === 'home' ? Object.fromEntries(Object.entries(v).filter(([k]) => k !== 'op')) : v)) })),
    }
    const servo = Catalog.fromGenerated(patched).require('servo')
    expect(servo.variants.find((v) => v.id === 'home')?.unsupported).toMatch(/no operation/)
    expect(servo.variants.find((v) => v.id === 'position')?.unsupported).toBeUndefined()
  })

  it('the op code is the u16 the firmware builds: (contract << 8) | n, or 0xFF00 | n for a device command', () => {
    const header = readFileSync(fileURLToPath(new URL('../../../../components/sys/sys_device/sys_device.h', import.meta.url)), 'utf8')
    expect(header).toMatch(/#define\s+SYS_DEVICE_OP\(contract,\s*n\)\s+\(\(uint16_t\)\(\(\(contract\)\s*<<\s*8\)\s*\|\s*\(n\)\)\)/)
    const contractNumbers = new Map([...header.matchAll(/#define\s+SYS_DEVICE_CONTRACT_(\w+)\s+0x([0-9a-fA-F]+)u/g)].map((m) => [m[1]!, parseInt(m[2]!, 16)]))
    const input = loadGeneratedInput()
    for (const contract of input.contracts) expect(contract.contract_id, `contract ${contract.id}`).toBe(contractNumbers.get(contract.id))
    // a device's own ops are `#define X_OP_NAME SYS_DEVICE_OWN_OP(0x..)` in the device's header
    const own = new Map<string, number>()
    for (const file of filesUnder('../devices', '.h')) {
      for (const [, symbol, n] of read(file).matchAll(/#define\s+(\w+)\s+SYS_DEVICE_OWN_OP\(\s*(0x[0-9a-fA-F]+|\d+)\s*\)/g)) own.set(symbol!, 0xff00 | Number(n))
    }
    let checked = 0
    for (const block of catalog.blocks) {
      for (const variant of block.variants) {
        const op = variant.operation
        if (!op) continue
        const source = op.source === 'contract' ? input.contracts.find((c) => c.id === op.of) : input.devices.find((d) => d.id === op.of)
        const command = (source?.commands as { id: string; op: number; symbol?: string }[]).find((c) => c.id === op.command)!
        if (op.source === 'contract') expect(op.value, `${block.id}.${variant.id}`).toBe((contractNumbers.get(op.of)! << 8) | command.op)
        else {
          expect(own.has(command.symbol!), `${command.symbol} is defined in the C of the device`).toBe(true)
          expect(op.value, `${block.id}.${variant.id} ${command.symbol}`).toBe(own.get(command.symbol!))
        }
        checked += 1
      }
    }
    expect(checked).toBeGreaterThan(30)
  })

  it('a scaled argument is a float input', () => {
    const drive = shapeOf(catalog.require('hbridge'), 'set_drive').inputs.find((p) => p.kind === 'pin' && p.id === 'drive')
    expect(drive?.kind === 'pin' && drive.value).toBe('float')
  })
})
