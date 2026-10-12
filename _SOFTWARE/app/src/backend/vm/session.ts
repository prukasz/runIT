import type { RouterClient } from '../protocol/client'
import { Program } from './program'
import { TelemetryMirror } from './telemetry'
import { batch, dataRecord, EXEC, encodeValue, FLAG, FRAME_BUDGET, KEY_VM, OP, T } from './wire'
import type { Value } from './wire'

const u16 = (n: number): Uint8Array => Uint8Array.of(n & 0xff, (n >> 8) & 0xff)

/** Most objects one SUBSCRIBE takes (the board answers ERR_VM_RANGE above it). */
export const SUBSCRIBE_MAX = 128

/**
 * The program on the board, from the app's side: load it, start and step it, write inputs, watch values.
 * Every command goes through the RouterClient, so a refusal is a `CommandError` naming the board's root cause.
 */
export class VmSession {
  readonly mirror: TelemetryMirror
  private loaded?: Program
  private mode: number = EXEC.reset
  private readonly stop: () => void
  private readonly client: RouterClient

  constructor(client: RouterClient) {
    this.client = client
    this.mirror = new TelemetryMirror((id) => {
      const o = this.loaded?.objects[id]
      return o ? { type: o.type, count: o.count } : undefined
    })
    this.stop = client.onTelemetry((frame) => this.mirror.apply(frame))
  }

  get program(): Program | undefined {
    return this.loaded
  }

  close(): void {
    this.stop()
  }

  private op(op: number, payload?: Uint8Array, timeoutMs?: number): Promise<Uint8Array> {
    return this.client.request([KEY_VM, op], payload, { timeoutMs })
  }

  /** Replaces the program on the board. Stops at the first refusal (the board is then left with a partial program: reset it). */
  async load(program: Program): Promise<void> {
    this.loaded = program
    this.mirror.clear()
    for (const frame of program.frames()) await this.op(frame.op, frame.payload, 3000)
  }

  /** Load and start (default: free-running). */
  async start(program: Program, command: number = EXEC.normal): Promise<void> {
    await this.load(program)
    await this.exec(command)
  }

  async exec(command: number): Promise<void> {
    await this.op(OP.exec, Uint8Array.of(command))
    if (([EXEC.scan, EXEC.normal, EXEC.block, EXEC.reset] as number[]).includes(command)) this.mode = command
  }

  /** Unloads the program and leaves the VM stopped. */
  async reset(): Promise<void> {
    await this.op(OP.reset)
    this.loaded = undefined
    this.mirror.clear()
    this.mode = EXEC.reset
  }

  /** Slows the program down (1 = normal speed). */
  async speed(factor: number): Promise<void> {
    await this.op(OP.speed, u16(factor))
  }

  /** Chooses the objects sent as telemetry (replaces the list; the board sends every value once, then the changes). */
  async subscribe(ids: readonly number[]): Promise<void> {
    if (ids.length > SUBSCRIBE_MAX) throw new RangeError(`At most ${SUBSCRIBE_MAX} objects can be subscribed (got ${ids.length}).`)
    await this.op(OP.subscribe, batch(ids.map(u16)))
  }

  /** Writes at run time (a fresh write: blocks that wait for a fresh input see it). */
  async write(id: number, value: Value, start = 0): Promise<void> {
    await this.writeRecords([this.record(id, value, start)])
  }

  /** Several objects in as few frames as fit. */
  async writes(values: ReadonlyMap<number, Value> | Readonly<Record<number, Value>>): Promise<void> {
    const entries = values instanceof Map ? [...values] : Object.entries(values).map(([id, v]) => [Number(id), v] as const)
    await this.writeRecords(entries.map(([id, value]) => this.record(id, value, 0)))
  }

  private record(id: number, value: Value, start: number): Uint8Array {
    const type = this.mirror.kind(id)?.type
    if (type === undefined) throw new RangeError(`Object ${id} is not in the loaded program.`)
    return dataRecord(id, encodeValue(type, value), start)
  }

  private async writeRecords(records: readonly Uint8Array[]): Promise<void> {
    let current: Uint8Array[] = []
    let size = 0
    for (const record of records) {
      if (current.length && size + record.length > FRAME_BUDGET) {
        await this.op(OP.setData, batch(current))
        current = []
        size = 0
      }
      current.push(record)
      size += record.length
    }
    if (current.length) await this.op(OP.setData, batch(current))
  }

  /**
   * Values uploaded with the program are not fresh: a block that waits for a fresh input does nothing until the app writes one at
   * run time. This writes every initial value of a mutable, unprotected object again. The board's override ring takes about 1 KB
   * per pass, so a long list goes in groups with a pass between them (a stepped VM is stepped, a free-running one waits).
   */
  async kick(perPass = 36, pause: (ms: number) => Promise<void> = (ms) => new Promise((r) => setTimeout(r, ms))): Promise<void> {
    const program = this.loaded
    if (!program) throw new Error('No program is loaded.')
    const writable = new Set(program.objects.filter((o) => o.flags & FLAG.mutable && !(o.flags & FLAG.usrProtected) && o.type !== T.ptr).map((o) => o.id))
    const records = program.initialRecords().filter((r) => writable.has(r.id)).map((r) => dataRecord(r.id, r.data, r.start))
    for (let at = 0; at < records.length; at += perPass) {
      await this.writeRecords(records.slice(at, at + perPass))
      if (at + perPass < records.length) {
        if (this.mode === EXEC.scan) await this.exec(EXEC.once)
        else await pause(80)
      }
    }
  }

  /** The current value of objects: subscribes (a fresh subscription sends everything once) and waits for them. */
  async snapshot(ids: readonly number[], timeoutMs = 2000): Promise<Map<number, number | boolean | number[] | Uint8Array | undefined>> {
    this.mirror.clear()
    await this.subscribe(ids)
    await this.mirror.waitFor(() => ids.every((id) => this.mirror.has(id)), timeoutMs)
    return new Map(ids.map((id) => [id, this.mirror.value(id)]))
  }

  /**
   * Stepped (scan) mode: applies `writes`, runs exactly one pass and returns the subscribed objects as they stand at its end.
   * The VM must be in scan mode (`start(program, EXEC.scan)`).
   */
  async step(ids: readonly number[], writes: ReadonlyMap<number, Value> | Readonly<Record<number, Value>> = {}, timeoutMs = 2000): Promise<Map<number, number | boolean | number[] | Uint8Array | undefined>> {
    if (writes instanceof Map ? writes.size : Object.keys(writes).length) await this.writes(writes)
    this.mirror.clear()
    await this.subscribe(ids)
    await this.exec(EXEC.once)
    await this.mirror.waitFor(() => ids.every((id) => this.mirror.has(id)), timeoutMs)
    return new Map(ids.map((id) => [id, this.mirror.value(id)]))
  }
}
