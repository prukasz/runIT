import type { BleDeviceRef, WebBluetoothPlatform } from '@backend'
import { withGesture } from '../desktop'
import type { Desktop } from '../desktop'
import { normalAddress } from './board'

/*
 * The desktop app's scan (electron/main.mjs): `requestDevice()` with no chooser of the browser's. Either it names the address it
 * wants and the main process picks that board as soon as the scan sees it (`find`), or the scan goes to the page as a list and the
 * user picks (`choose`, answered by `select`). Electron runs one chooser at a time: the scans here wait for each other, and
 * `cancel` ends the one under way.
 */

export class DesktopScanner {
  private queue: Promise<unknown> = Promise.resolve()
  private picked: string | undefined

  constructor(private readonly bridge: Desktop, private readonly platform: WebBluetoothPlatform) {}

  /** The board with this address, when a scan sees it within `ms`; undefined otherwise (or when cancelled). */
  find(address: string, ms: number): Promise<BleDeviceRef | undefined> {
    return this.exclusive(() => this.scan(address, ms).catch(() => undefined))
  }

  /** The scan as a list (`Desktop.onDevices`) until the user picks a board (`select`) or cancels (rejects). */
  choose(): Promise<{ readonly ref: BleDeviceRef; readonly address: string }> {
    return this.exclusive(async () => {
      this.picked = undefined
      const ref = await this.scan(null)
      const address = this.picked && normalAddress(this.picked)
      if (!address) throw new Error('The scan gave no address for the board.')
      return { ref, address }
    })
  }

  /** The user's pick in the list of `choose`. */
  select(id: string): void {
    this.picked = id
    this.bridge.select(id)
  }

  /** Ends the scan under way: its `find` gives undefined, its `choose` rejects. */
  cancel(): void {
    this.bridge.select('')
  }

  private scan(address: string | null, ms?: number): Promise<BleDeviceRef> {
    // `requestDevice()` needs a user gesture: the main process gives one, and the call must come before the first await.
    return withGesture(this.bridge, async () => {
      this.bridge.want(address)
      const timer = ms === undefined ? undefined : setTimeout(() => this.bridge.select(''), ms)
      try {
        return await this.platform.requestDevice()
      } finally {
        clearTimeout(timer)
        this.bridge.want(null)
      }
    })
  }

  private exclusive<T>(work: () => Promise<T>): Promise<T> {
    const result = this.queue.then(work)
    this.queue = result.catch(() => undefined)
    return result
  }
}
