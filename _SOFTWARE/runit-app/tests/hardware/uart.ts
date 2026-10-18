import { Helper } from './bridge'

export interface ConsoleLine {
  readonly at: number
  readonly text: string
}

/** The board's console as an independent witness: what the firmware logged, whatever the BLE link says. */
export class Console {
  private readonly helper: Helper
  readonly lines: ConsoleLine[] = []

  constructor(port: string) {
    this.helper = new Helper('uart_tap.py', [port])
    this.helper.onEvent((m) => {
      if (m.event === 'line') this.lines.push({ at: (m.at as number) * 1000, text: m.text as string })
    })
  }

  /** Lines logged since `since` (ms since the epoch) that match. */
  since(since: number, pattern?: RegExp): ConsoleLine[] {
    return this.lines.filter((l) => l.at >= since && (!pattern || pattern.test(l.text)))
  }

  /** Waits for a line that matches, from `since` on. */
  async waitFor(pattern: RegExp, since: number, timeoutMs = 5000): Promise<ConsoleLine | undefined> {
    const end = Date.now() + timeoutMs
    while (Date.now() < end) {
      const hit = this.since(since, pattern)[0]
      if (hit) return hit
      await new Promise((r) => setTimeout(r, 25))
    }
    return undefined
  }

  /** A panic or a crash the test did not ask for. */
  trouble(since: number): ConsoleLine[] {
    return this.since(since, /Guru Meditation|abort\(\) was called|Backtrace:|assert failed|stack overflow|Rebooting\.\.\./i)
  }

  reset(): void {
    this.helper.send('reset')
  }

  stop(): void {
    this.helper.stop()
  }
}
