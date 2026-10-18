/**
 * The desktop app's board chooser (electron/main.mjs, electron/preload.cjs); `undefined` in a browser. A scan result's `id` is
 * the one the chooser knows, not the `BluetoothDevice.id` the page sees: it is what is remembered to pick the board again.
 */
export interface ScanResult {
  readonly id: string
  readonly name: string
}

export interface Desktop {
  /** The board to pick as soon as the scan sees it; `null`: the scan goes to `onDevices` for the user to choose. */
  want(id: string | null): void
  /** Answers the open chooser; '' cancels it (`requestDevice()` then rejects). */
  select(id: string): void
  /** Runs `globalThis.__runitGesture` with a user gesture. */
  gesture(): Promise<void>
  onDevices(handler: (devices: readonly ScanResult[]) => void): () => void
}

const scope = globalThis as { runitDesktop?: Desktop; __runitGesture?: () => void }

export const desktop: Desktop | undefined = scope.runitDesktop

/** Runs `start` with a user gesture, so it may call `requestDevice()` with no click. It must call it before its first await. */
export function withGesture<T>(bridge: Desktop, start: () => Promise<T>): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    scope.__runitGesture = () => {
      scope.__runitGesture = undefined
      start().then(resolve, reject)
    }
    bridge.gesture().catch(reject)
  })
}
