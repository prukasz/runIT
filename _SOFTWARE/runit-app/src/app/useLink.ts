import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { RunitLink, Services, WebBluetoothPlatform, bundledLinkDefinitions } from '@backend'
import type { BleDeviceRef, LinkState } from '@backend'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { desktop, withGesture } from './desktop'
import type { Desktop, ScanResult } from './desktop'

const BOARD_KEY = 'runit.board'
/** How long the desktop app looks for the remembered board before it says the board is not nearby. */
const FIND_MS = 20_000

/** What a Chromium browser needs before a reloaded page can open the board with no chooser (`getDevices`, `watchAdvertisements`). */
const RESTORE_FLAGS = 'This browser cannot reconnect after a reload. Enable "Use the new permissions backend for Web Bluetooth" and '
  + '"Experimental Web Platform features" in chrome://flags (edge://flags), restart it and connect once. Until then press Connect.'

/** The user's board: as the page sees it, and in the desktop app the id its chooser picks it by (`scanId`). */
interface Board extends BleDeviceRef {
  readonly scanId?: string
}

const parseBoard = (raw: string): Board | undefined => {
  const value: unknown = JSON.parse(raw)
  if (typeof value !== 'object' || value === null || typeof (value as Board).id !== 'string') return undefined
  const { id, name, scanId } = value as Board
  return { id, ...(typeof name === 'string' ? { name } : {}), ...(typeof scanId === 'string' ? { scanId } : {}) }
}

/**
 * The link to the board as the screens use it: its state, the board's name, and Connect / Disconnect / Forget. The backend does
 * the work (the connection that keeps itself up); this holds what a screen shows and which board is "mine".
 *
 * In a browser, Connect opens that board with no chooser when the browser still allows it, and shows the browser's chooser
 * otherwise. In the desktop app there is no browser chooser: the remembered board is picked as soon as the scan sees it, and
 * with none remembered the scan is `scan`, a list for the panel, answered with `choose` / `cancelScan`. With `auto` the
 * remembered board is opened once when the page loads (on the desktop with no click). Forget drops the link, the permission and
 * the remembered board.
 */
export function useLink(auto: boolean) {
  const platform = useMemo(() => new WebBluetoothPlatform(bundledLinkDefinitions()), [])
  const link = useMemo(() => new RunitLink(platform), [platform])
  const [state, setState] = useState<LinkState>(link.state)
  const [reason, setReason] = useState<string | undefined>()
  const [board, setBoard] = useState<Board | undefined>(() => readStored(BOARD_KEY, parseBoard, undefined))
  const [scan, setScan] = useState<readonly ScanResult[] | undefined>()
  const restored = useRef(false)
  const picked = useRef<string | undefined>(undefined)

  useEffect(() => link.onStatus((status) => { setState(status.state); setReason(status.reason) }), [link])

  // The board is told when the app leaves on purpose (SYS BYE: it turns a dead-man off and goes on with its program), so a silence
  // that follows is not taken for a lost link. A firmware without BYE refuses it: nothing is lost by asking.
  const bye = useCallback(async () => {
    if (link.state !== 'connected') return
    await new Services(link).call('SYS', 'BYE', undefined, { timeoutMs: 800, label: 'SYS BYE' }).catch(() => undefined)
  }, [link])
  // A closing window says it too, as far as the page still can.
  useEffect(() => {
    const leaving = () => void bye()
    window.addEventListener('pagehide', leaving)
    return () => window.removeEventListener('pagehide', leaving)
  }, [bye])
  useEffect(() => desktop?.onDevices((devices) => setScan(devices)), [])

  const remember = useCallback((next: Board | undefined) => {
    setBoard(next)
    if (next) writeStored(BOARD_KEY, JSON.stringify(next))
    else {
      try { localStorage.removeItem(BOARD_KEY) } catch { /* a preference only */ }
    }
  }, [])

  /** The remembered board among those the browser still allows; a single allowed board when none is remembered. */
  const known = useCallback(async () => {
    const devices = await platform.knownDevices()
    const saved = readStored(BOARD_KEY, parseBoard, undefined)
    return devices.find((device) => device.id === saved?.id) ?? (devices.length === 1 ? devices[0] : undefined)
  }, [platform])

  /** The desktop chooser: `saved` picked when the scan sees it, else the scan shown for the user to choose. */
  const scanFor = useCallback((bridge: Desktop, saved: Board | undefined) => async (): Promise<Board> => {
    picked.current = saved?.scanId
    bridge.want(saved?.scanId ?? null)
    let missing = false
    const timer = saved?.scanId ? setTimeout(() => { missing = true; bridge.select('') }, FIND_MS) : undefined
    setScan([])
    try {
      const device = await platform.requestDevice()
      return { ...device, ...(picked.current ? { scanId: picked.current } : {}) }
    } catch (error) {
      if (missing) throw new Error(`${saved?.name ?? 'The board'} was not found nearby. Is it switched on?`)
      throw error
    } finally {
      clearTimeout(timer)
      bridge.want(null)
      setScan(undefined)
    }
  }, [platform])

  const open = useCallback(async (choose: () => Promise<Board | undefined>) => {
    setReason(undefined)
    try {
      const chosen = await choose()
      if (!chosen) return
      remember(chosen)
      await link.connect(chosen)
    } catch (error) {
      // A chooser the user closed is not a failure to report.
      if (!(error instanceof Error && /cancel/i.test(error.message))) setReason(error instanceof Error ? error.message : String(error))
    }
  }, [link, remember])

  const connect = useCallback(
    () => open(desktop ? scanFor(desktop, board) : async () => (await known()) ?? platform.requestDevice()),
    [open, scanFor, board, known, platform],
  )
  const disconnect = useCallback(async () => { await bye(); await link.disconnect() }, [link, bye])
  const forget = useCallback(async () => {
    await bye()
    await link.disconnect()
    const current = board ?? (await known())
    if (current) await platform.forget(current)
    remember(undefined)
    setReason(undefined)
  }, [link, bye, board, known, platform, remember])

  const choose = useCallback((id: string) => {
    picked.current = id
    desktop?.select(id)
  }, [])
  const cancelScan = useCallback(() => desktop?.select(''), [])

  // Once per page load (StrictMode runs effects twice): the remembered board, no chooser. When the browser cannot do that, the
  // panel says why instead of staying silent.
  useEffect(() => {
    if (restored.current) return
    restored.current = true
    if (!auto || !platform.available || !board) return
    if (desktop) {
      if (board.scanId) void open(() => withGesture(desktop!, scanFor(desktop!, board)))
      return
    }
    if (!platform.remembersBoards) return setReason(RESTORE_FLAGS)
    void open(async () => {
      const found = await known()
      if (!found) setReason(`The browser no longer allows ${board.name ?? 'the board'}: press Connect and choose it.`)
      return found
    })
  }, [auto, open, known, scanFor, platform, board])

  return {
    link, state, reason, scan, choose, cancelScan,
    device: board ? board.name ?? board.id : undefined,
    available: platform.available,
    connect, disconnect, forget,
  }
}
