import { useCallback, useEffect, useRef, useState } from 'react'
import { BRIDGE_UART_PROVIDER, BridgePlatform, DEFAULT_WIRE, REMOTE_BOARD, RunitLink, Services, Sessions } from '@backend'
import type { BridgeWire, LinkState, LinkStatus, SessionPeer } from '@backend'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { parseRemote, remoteReason } from './remote'
import type { Perspective, StoredRemote } from './remote'

/*
 * Seamless dual-mode link to the wired remote board behind the main board:
 *
 *   - On the main board (`perspective === 'main'`, once detected): hands the UART to the master board itself as a
 *     `SYS_SESSION_SLAVE` session (`platform.attachSlave()`), so the master's VM can call the slave's devices directly.
 *   - On the remote board (`perspective === 'remote'`, or during initial Connect & detect): detaches the master session
 *     and bridges the UART to BLE (`BridgePlatform`) so the app talks to the slave directly in its own workspace.
 *
 * Switching perspectives flips the UART role automatically, and after a Write on the master (which sweeps devices),
 * `reattachSlave()` brings the master's slave session right back.
 */

const KEY = 'runit.remote.link'
/** One attempt: the UART and its links made on the main board, then the other board's hello. */
const OPEN_TIMEOUT_MS = 8000
/** Keepalive while the app holds the UART tunnel. */
const PING_EVERY_MS = 3000
/** How often PEERS is checked while the master holds the slave session. */
const SLAVE_POLL_MS = 2500

export type RemoteMode = 'off' | 'app' | 'master'

interface Held {
  readonly platform: BridgePlatform
  readonly link: RunitLink
  wire: BridgeWire
}

export function useRemote(master: RunitLink, masterState: LinkState, perspective: Perspective) {
  const [stored, setStored] = useState<StoredRemote>(() => readStored(KEY, parseRemote, { on: false, wire: DEFAULT_WIRE }))
  const [detecting, setDetecting] = useState(false)
  const [slavePeer, setSlavePeer] = useState<SessionPeer | undefined>()
  const [slaveError, setSlaveError] = useState<string | undefined>()
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

  // Desired UART mode: app tunnel while viewing the remote board or detecting it; master slave-session otherwise.
  const mode: RemoteMode = !stored.on || masterState !== 'connected'
    ? 'off'
    : (perspective === 'remote' || detecting)
    ? 'app'
    : 'master'

  // Serialize UART role transitions so rapid perspective switches never race.
  const chain = useRef<Promise<void>>(Promise.resolve())
  const enqueue = useCallback((fn: () => Promise<void>) => {
    const next = chain.current.then(fn, fn)
    chain.current = next.catch(() => undefined)
    return next
  }, [])

  const closeAppTunnel = useCallback(async () => {
    if (link.state === 'connected') {
      await new Services(link).call('SYS', 'BYE', undefined, { timeoutMs: 500, label: 'SYS BYE (remote)' }).catch(() => undefined)
    }
    if (link.state !== 'idle' && link.state !== 'closed') {
      await link.disconnect().catch(() => undefined)
    }
  }, [link])

  useEffect(() => {
    let alive = true
    if (mode === 'off') {
      setSlavePeer(undefined)
      setSlaveError(undefined)
      return () => { alive = false }
    }
    if (mode === 'app') {
      setSlavePeer(undefined)
      setSlaveError(undefined)
      void enqueue(async () => {
        if (!alive) return
        if (link.state === 'idle') void link.connect(REMOTE_BOARD, { retry: true }).catch(() => undefined)
      })
      return () => { alive = false }
    }
    // mode === 'master': close any app tunnel and hand UART 63 to the master as a slave-board session.
    void enqueue(async () => {
      if (!alive || master.state !== 'connected') return
      await closeAppTunnel()
      if (!alive || master.state !== 'connected') return
      try {
        await platform.attachSlave()
        if (alive) setSlaveError(undefined)
      } catch (e) {
        if (alive) setSlaveError(e instanceof Error ? e.message : String(e))
      }
    })
    return () => { alive = false }
  }, [mode, master, link, platform, enqueue, closeAppTunnel])

  // Keepalive while in app tunnel mode.
  useEffect(() => {
    if (mode !== 'app' || status.state !== 'connected') return
    const services = new Services(link)
    const timer = setInterval(() => void services.call('SYS', 'PING', undefined, { label: 'SYS PING (remote)' }).catch(() => undefined), PING_EVERY_MS)
    return () => clearInterval(timer)
  }, [mode, status.state, link])

  // Poll PEERS while in master mode: updates `slavePeer` status and heals if UART 63 was swept.
  useEffect(() => {
    if (mode !== 'master') return
    let alive = true
    const sessions = new Sessions(master)
    const poll = async () => {
      if (!alive || master.state !== 'connected') return
      try {
        const list = await sessions.peers({ timeoutMs: 1200 })
        if (!alive) return
        const peer = list.find((p) => p.provider === BRIDGE_UART_PROVIDER)
        setSlavePeer(peer)
        if (!peer) {
          // Master lost the session (e.g. after a restart or sweep): re-attach automatically.
          void enqueue(async () => {
            if (!alive || master.state !== 'connected') return
            await platform.attachSlave().catch(() => undefined)
          })
        }
      } catch {
        // Next tick retries.
      }
    }
    const start = setTimeout(() => void poll(), 400)
    const timer = setInterval(() => void poll(), SLAVE_POLL_MS)
    return () => { alive = false; clearTimeout(start); clearInterval(timer) }
  }, [mode, master, platform, enqueue])

  const connect = useCallback((wire: BridgeWire) => {
    setSlaveError(undefined)
    setDetecting(true)
    keep({ on: true, wire })
  }, [keep])

  const finishDetect = useCallback(() => {
    setDetecting(false)
  }, [])

  /** Re-attaches the master's slave-board UART session (called right after a master Write sweeps devices). */
  const reattachSlave = useCallback(async () => {
    if (!stored.on || master.state !== 'connected' || perspective !== 'main' || detecting) return
    await enqueue(async () => {
      if (master.state !== 'connected') return
      try {
        await platform.attachSlave()
        setSlaveError(undefined)
      } catch (e) {
        setSlaveError(e instanceof Error ? e.message : String(e))
      }
    })
  }, [stored.on, master, perspective, detecting, enqueue, platform])

  /** Drops the link and removes the bridge/slave UART and session from the master. */
  const disconnect = useCallback(async () => {
    setDetecting(false)
    setSlavePeer(undefined)
    setSlaveError(undefined)
    keep({ on: false, wire: held.current!.wire })
    await enqueue(async () => {
      await closeAppTunnel()
      if (master.state === 'connected') await platform.release().catch(() => undefined)
    })
  }, [keep, enqueue, closeAppTunnel, master, platform])

  return {
    link,
    state: status.state,
    reason: status.reason,
    mode,
    detecting,
    finishDetect,
    slavePeer,
    slaveError,
    reattachSlave,
    /** Wanted: connected (either linked to master or open as app tunnel). */
    on: stored.on,
    wire: stored.wire,
    setWire: (wire: BridgeWire) => keep({ on: stored.on, wire }),
    connect,
    disconnect,
  }
}

export type RemoteState = ReturnType<typeof useRemote>
