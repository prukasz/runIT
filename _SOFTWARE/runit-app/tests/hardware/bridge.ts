import { spawn } from 'node:child_process'
import type { ChildProcessWithoutNullStreams } from 'node:child_process'
import { createInterface } from 'node:readline'
import { fileURLToPath } from 'node:url'

/* A Python helper behind JSON lines: bleak for the radio (bleak_bridge.py), pyserial for the console (uart_tap.py). */

/** The repository's venv has bleak and pyserial; HW_PYTHON overrides it. */
export const PYTHON = process.env.HW_PYTHON ?? fileURLToPath(new URL('../../../.venv/Scripts/python.exe', import.meta.url))
const script = (name: string) => fileURLToPath(new URL(name, import.meta.url))

export type Message = Record<string, unknown>

export class Helper {
  private readonly child: ChildProcessWithoutNullStreams
  private nextId = 1
  private readonly pending = new Map<number, { resolve: (m: Message) => void; reject: (e: Error) => void }>()
  private readonly listeners = new Set<(m: Message) => void>()
  readonly stderr: string[] = []

  constructor(file: string, args: string[] = []) {
    this.child = spawn(PYTHON, [script(file), ...args], { stdio: ['pipe', 'pipe', 'pipe'] })
    this.child.stderr.on('data', (d: Buffer) => this.stderr.push(d.toString('utf8')))
    createInterface({ input: this.child.stdout }).on('line', (line) => {
      let m: Message
      try {
        m = JSON.parse(line) as Message
      } catch {
        return
      }
      const id = m.id as number | undefined
      const waiter = id === undefined ? undefined : this.pending.get(id)
      if (waiter && id !== undefined) {
        this.pending.delete(id)
        if (m.error !== undefined) waiter.reject(new Error(String(m.error)))
        else waiter.resolve(m)
      } else for (const l of this.listeners) l(m)
    })
    this.child.on('exit', () => {
      for (const w of this.pending.values()) w.reject(new Error(`helper exited: ${this.stderr.join('').slice(-400)}`))
      this.pending.clear()
    })
  }

  onEvent(listener: (m: Message) => void): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  call(cmd: string, args: Message = {}): Promise<Message> {
    const id = this.nextId++
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.child.stdin.write(`${JSON.stringify({ id, cmd, ...args })}\n`)
    })
  }

  /** A command with no answer (the console helper has none). */
  send(cmd: string): void {
    this.child.stdin.write(`${JSON.stringify({ cmd })}\n`)
  }

  stop(): void {
    this.child.stdin.end()
    this.child.kill()
  }
}
