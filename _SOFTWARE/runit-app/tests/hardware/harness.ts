import { RunitLink, Services, bundledLinkDefinitions } from '@backend'
import type { LinkOptions, LinkStatus } from '@backend'
import { BleakPlatform } from './bleakPlatform'

export const defs = bundledLinkDefinitions()

/** The board under test: a link over BLE through the PC's radio. */
export interface Board {
  readonly platform: BleakPlatform
  readonly link: RunitLink
  readonly services: Services
  readonly statuses: LinkStatus[]
  /** When the suite opened the link (ms since the epoch). */
  readonly startedAt: number
  close(): Promise<void>
}

/** HW_NAME is the advertised name (default runit), HW_ADDRESS pins a board, HW_PYTHON the Python with bleak (default ../.venv). */
export async function openBoard(options: LinkOptions = {}): Promise<Board> {
  const platform = new BleakPlatform(defs, { name: process.env.HW_NAME, address: process.env.HW_ADDRESS })
  const link = new RunitLink(platform, { timeoutMs: 3000, ...options })
  const statuses: LinkStatus[] = []
  link.onStatus((s) => statuses.push(s))
  const startedAt = Date.now()
  await link.connect(await platform.requestDevice())
  return {
    platform,
    link,
    services: new Services(link),
    statuses,
    startedAt,
    close: async () => {
      await link.disconnect()
      platform.stop()
    },
  }
}

export const sleep = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms))

/** Polls until `check` returns something, or `timeoutMs` pass (then undefined). */
export async function until<T>(check: () => T | undefined | false | Promise<T | undefined | false>, timeoutMs = 5000, stepMs = 25): Promise<T | undefined> {
  const end = Date.now() + timeoutMs
  for (;;) {
    const value = await check()
    if (value) return value
    if (Date.now() >= end) return undefined
    await sleep(stepMs)
  }
}

/** After a restart: waits for the link to be up again and answering. */
export async function backUp(board: Board, timeoutMs = 20_000): Promise<number | undefined> {
  return until(async () => {
    try {
      const { value } = await board.services.call('SYS', 'PING')
      return ((value as { uptime_ms: number }).uptime_ms ?? 0) + 1
    } catch {
      return undefined
    }
  }, timeoutMs, 250)
}
