import { useCallback, useEffect, useRef, useState } from 'react'
import { BridgePlatform, DEFAULT_WIRE, REMOTE_BOARD, RunitLink, Services } from '@backend'
import type { BridgeWire, LinkState, LinkStatus } from '@backend'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { parseRemote, remoteReason } from './remote'
import type { StoredRemote } from './remote'

/*
 * The remote board for the screens: the board behind the one the app is connected to (the main board), reached through it
 * (backend/session/bridge.ts). Its link is a session of its own, beside the main board's. Connected once, it is wanted until
 * Disconnect, also after the app restarts: its link waits for the main board and comes back with it.
 */

const KEY = 'runit.remote.link'
/** One attempt: the UART and its links made on the main board, then the other board's hello. */
const OPEN_TIMEOUT_MS = 8000
/** With nothing else on the way, this often the other board is asked whether it is there: on a UART nothing else says it is gone. */
const PING_EVERY_MS = 3000

interface Held {
  readonly platform: BridgePlatform
  readonly link: RunitLink
  wire: BridgeWire
}

export function useRemote(master: RunitLink) {
  const [stored, setStored] = useState<StoredRemote>(() => readStored(KEY, parseRemote, { on: false, wire: DEFAULT_WIRE }))
  const held = useRef<Held | undefined>(undefined)
  if (!held.current) {
    const made = { wire: stored.wire } as { wire: BridgeWire; platform: BridgePlatform; link: RunitLink }
    made.platform = new BridgePlatform(master, () => made.wire)
    made.link = new RunitLink(made.platform, { reconnect: { delaysMs: [0, 500, 1000, 2000, 4000], openTimeoutMs: OPEN_TIMEOUT_MS }, timeoutMs: 3000 })
    held.current = made
  }
  const { platform, link } = held.current
  held.current.wire = stored.wire
  const [status, setStatus] = useState<{ readonly state: LinkState; readonly reason?: string }>({ state: link.state })

  useEffect(() => link.onStatus((next: LinkStatus) => {
    const reason = next.state === 'connected' ? undefined : remoteReason(next.reason, platform.heard, master.state === 'connected')
    setStatus({ state: next.state, ...(reason ? { reason } : {}) })
  }), [link, master, platform])

  const keep = useCallback((next: StoredRemote) => {
    setStored(next)
    writeStored(KEY, JSON.stringify(next))
  }, [])

  useEffect(() => {
    // The link keeps trying by itself until it works or is disconnected.
    if (stored.on && link.state === 'idle') void link.connect(REMOTE_BOARD, { retry: true }).catch(() => undefined)
  }, [stored.on, link])

  useEffect(() => {
    if (status.state !== 'connected') return
    const services = new Services(link)
    const timer = setInterval(() => void services.call('SYS', 'PING', undefined, { label: 'SYS PING (remote)' }).catch(() => undefined), PING_EVERY_MS)
    return () => clearInterval(timer)
  }, [status.state, link])

  const connect = useCallback((wire: BridgeWire) => keep({ on: true, wire }), [keep])

  /** A goodbye to the other board, its link dropped, and the bridge's UART taken off the main board. */
  const disconnect = useCallback(async () => {
    keep({ on: false, wire: held.current!.wire })
    if (link.state === 'connected') await new Services(link).call('SYS', 'BYE', undefined, { timeoutMs: 800, label: 'SYS BYE (remote)' }).catch(() => undefined)
    await link.disconnect()
    if (master.state === 'connected') await platform.release().catch(() => undefined)
  }, [keep, link, master, platform])

  return {
    link,
    state: status.state,
    reason: status.reason,
    /** Wanted: connected, or on its way. */
    on: stored.on,
    wire: stored.wire,
    setWire: (wire: BridgeWire) => keep({ on: stored.on, wire }),
    connect,
    disconnect,
  }
}

export type RemoteState = ReturnType<typeof useRemote>
