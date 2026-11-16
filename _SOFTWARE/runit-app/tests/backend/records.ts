import { errorCatalog } from '@backend'
import { defs } from './fakes'

const catalog = errorCatalog()
export const tagOf = (name: string) => [...catalog.values()].find((e) => e.name === name)!.tag

export const u16 = (n: number) => [n & 0xff, n >> 8]
export const u32 = (n: number) => [n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, n >>> 24]

/** The payload of an error from its field values, laid out as errors.json says. */
export const payload = (name: string, values: Record<string, number> = {}): number[] => {
  const info = catalog.get(tagOf(name))!
  const out = new Array<number>(info.fields.reduce((n, f) => Math.max(n, f.offset + f.size), 0)).fill(0)
  for (const f of info.fields) {
    const v = values[f.name] ?? 0
    const bytes = f.type === 'f32' ? [...new Uint8Array(Float32Array.of(v).buffer)] : f.size === 1 ? [v & 0xff] : f.size === 2 ? u16(v) : u32(v)
    out.splice(f.offset, f.size, ...bytes)
  }
  return out
}

/** An error record (ERRORS.MD "Error record"): a command's when `request` is given, else an internal one. */
export const record = (nodes: [string, Record<string, number>?][], request?: number[], seq = 7, time = 0): Uint8Array => {
  const stamp = [...u32(time % 2 ** 32), ...u32(Math.floor(time / 2 ** 32))] // u64 LE
  const head = request ? [defs.errorRecord.srcCommand, ...stamp, 0xf0, ...u16(seq), 0, request.length, ...request] : [defs.errorRecord.srcInternal, ...stamp]
  const chain = nodes.flatMap(([name, values]) => {
    const info = catalog.get(tagOf(name))!
    const p = payload(name, values)
    return [...u32(info.tag), p.length, ...p]
  })
  return Uint8Array.from([...head, nodes.length, ...chain])
}
