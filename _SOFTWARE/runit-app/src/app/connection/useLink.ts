import { useCallback, useEffect, useRef, useState } from 'react'
import { RunitLink, Services, WebBluetoothPlatform, bundledLinkDefinitions } from '@backend'
import type { LinkState } from '@backend'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { desktop } from '../desktop'
import type { ScanResult } from '../desktop'
import { parseRemembered } from './board'
import type { RememberedBoard } from './board'
import { BoardConnection } from './connection'
import type { BoardStore, ConnectionState } from './connection'
import { browserFinder, desktopFinder, SCAN_MS } from './finders'
import { DesktopScanner } from './scanner'

/*
 * The connection for the screens (connection.ts does the work): the link's state, the user's board, Connect / Disconnect / Forget,
 * the desktop app's list of boards nearby while the user picks one. `auto`: the remembered board is connected when the page
 * loads and as soon as the switch is turned on.
 */

const BOARD_KEY = 'runit.board'

const store: BoardStore = {
  read: () => readStored(BOARD_KEY, parseRemembered, undefined),
  write: (board) => {
    if (board) writeStored(BOARD_KEY, JSON.stringify(board))
    else {
      try { localStorage.removeItem(BOARD_KEY) } catch { /* a preference only */ }
    }
  },
}

/** One attempt to open: a scan for the board (`SCAN_MS`) and its connection fit in it. */
const OPEN_TIMEOUT_MS = SCAN_MS + 12_000

/** One connection for the page's life: a hot reload re-runs the hooks, and a new link would be an idle one under a connected screen. */
interface Held {
  readonly connection: BoardConnection
  readonly scanner: DesktopScanner | undefined
  readonly available: boolean
}

const make = (): Held => {
  let connection!: BoardConnection
  const bridge = desktop
  const platform = new WebBluetoothPlatform(bundledLinkDefinitions(), undefined, bridge ? { find: () => connection.findAgain() } : {})
  const scanner = bridge && new DesktopScanner(bridge, platform)
  const link = new RunitLink(platform, { reconnect: { openTimeoutMs: OPEN_TIMEOUT_MS } })
  connection = new BoardConnection(link, scanner ? desktopFinder(scanner, platform) : browserFinder(platform), store)
  return { connection, scanner, available: platform.available }
}

export function useLink(auto: boolean) {
  const held = useRef<Held | undefined>(undefined)
  held.current ??= make()
  const { connection, scanner, available } = held.current
  const [state, setState] = useState<ConnectionState>(connection.current)
  const [scan, setScan] = useState<readonly ScanResult[]>([])
  useEffect(() => connection.subscribe(setState), [connection])
  useEffect(() => desktop?.onDevices(setScan), [])
  useEffect(() => { if (!state.choosing) setScan([]) }, [state.choosing])

  // Connect automatically: at load, and when the switch is turned on.
  useEffect(() => { if (auto && available) connection.auto() }, [auto, connection])

  // The desktop app's window closing: a proper goodbye (SYS BYE, the link dropped) before it goes.
  useEffect(() => desktop?.onLeaving(() => connection.disconnect()), [connection])
  // A browser's tab closing says goodbye as far as the page still can, so its silence is not taken for a lost link.
  useEffect(() => {
    const leaving = () => {
      if (connection.link.state === 'connected') void new Services(connection.link).call('SYS', 'BYE', undefined, { timeoutMs: 800, label: 'SYS BYE' }).catch(() => undefined)
    }
    window.addEventListener('pagehide', leaving)
    return () => window.removeEventListener('pagehide', leaving)
  }, [connection])

  const choose = useCallback((id: string) => scanner?.select(id), [scanner])
  // While the board is looked for the link has no state of its own yet: the screens show it as connecting.
  const linkState: LinkState = state.searching || state.choosing ? 'connecting' : state.link
  return {
    link: connection.link,
    state: linkState,
    reason: state.reason,
    board: state.board as RememberedBoard | undefined,
    searching: state.searching,
    /** The desktop app's boards nearby while the user picks one; undefined otherwise. */
    scan: desktop && state.choosing ? scan : undefined,
    available,
    choose,
    cancelScan: () => connection.cancelChoice(),
    connect: () => connection.connect(),
    disconnect: () => connection.disconnect(),
    forget: () => connection.forget(),
  }
}
