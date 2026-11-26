import type { WebBluetoothPlatform } from '@backend'
import { NeedsUser } from './connection'
import type { BoardFinder } from './connection'
import type { DesktopScanner } from './scanner'

/*
 * The two ways a board is found (connection.ts `BoardFinder`). The desktop app scans for the board's address, so the board is
 * found whenever it is switched on and in range. A browser shows no address and scans only in its chooser: it opens the board
 * it allowed before (`getDevices()`, by its id), which needs two Chromium flags, and the chooser otherwise.
 */

/** How long one scan for the board lasts before a pause (connection.ts) and the next. */
export const SCAN_MS = 8000

/** The name a board gets when it advertises none. */
const UNNAMED = 'runit'

/** What a Chromium browser needs before a reloaded page can open the board with no chooser (`getDevices`, `watchAdvertisements`). */
export const RESTORE_FLAGS = 'This browser cannot reconnect after a reload. Enable "Use the new permissions backend for Web Bluetooth" and '
  + '"Experimental Web Platform features" in chrome://flags (edge://flags), restart it and connect once. Until then press Connect.'

export const desktopFinder = (scanner: DesktopScanner, platform: WebBluetoothPlatform): BoardFinder => ({
  find: async (board) => {
    if (!board.address) throw new NeedsUser(`${board.name} has no address remembered: pick it from the list.`)
    return scanner.find(board.address, SCAN_MS)
  },
  choose: async () => {
    const { ref, address } = await scanner.choose()
    return { ref, board: { name: ref.name || UNNAMED, address } }
  },
  cancel: () => scanner.cancel(),
  forget: async (ref) => { if (ref) await platform.forget(ref) },
})

export const browserFinder = (platform: WebBluetoothPlatform): BoardFinder => ({
  find: async (board) => {
    if (!platform.remembersBoards) throw new NeedsUser(RESTORE_FLAGS)
    const found = board.browserId === undefined ? undefined : (await platform.knownDevices()).find((d) => d.id === board.browserId)
    if (!found) throw new NeedsUser(`The browser no longer allows ${board.name}: press Connect and choose it.`)
    return found
  },
  choose: async () => {
    const ref = await platform.requestDevice()
    return { ref, board: { name: ref.name || UNNAMED, browserId: ref.id } }
  },
  cancel: () => undefined,
  forget: async (ref) => { if (ref) await platform.forget(ref) },
})
