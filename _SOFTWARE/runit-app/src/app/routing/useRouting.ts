import { useCallback, useEffect, useMemo, useState } from 'react'
import { bleTableCall, decodeBleTable, decodePeers, PROVIDER_COMMANDS, Services, SESSION_COMMANDS } from '@backend'
import type { FieldValues, RunitLink } from '@backend'
import { decodeMatrixLinks, decodeProviderRows, offlinePreviewSnapshot, SESSION_SAFETY_FLAG } from './routing'
import type { RoutingSnapshot } from './routing'

const errorText = (err: unknown): string => (err instanceof Error ? err.message : String(err))

export interface RoutingState {
  readonly connected: boolean
  readonly live: boolean
  readonly snapshot: RoutingSnapshot
  readonly busy?: string
  readonly error?: string
  readonly note?: string
  readonly services: Services
  readonly link: RunitLink
  refresh: () => Promise<void>
  createProvider: (id: number, type: string | number, config: FieldValues) => Promise<boolean>
  removeProvider: (id: number) => Promise<boolean>
  attachSession: (provider: number, deadmanMs: number, safety: boolean, modules: readonly number[]) => Promise<boolean>
  detachSession: (provider: number) => Promise<boolean>
  connectPorts: (from: number, to: number, bothWays?: boolean) => Promise<boolean>
  disconnectPorts: (from: number, to: number) => Promise<boolean>
  clearFeedback: () => void
}

export function useRouting(link: RunitLink, connected: boolean): RoutingState {
  const services = useMemo(() => new Services(link), [link])
  const preview = useMemo(() => offlinePreviewSnapshot(link.defs), [link.defs])
  const [liveSnapshot, setLiveSnapshot] = useState<RoutingSnapshot | undefined>()
  const [busy, setBusy] = useState<string | undefined>()
  const [error, setError] = useState<string | undefined>()
  const [note, setNote] = useState<string | undefined>()

  const refresh = useCallback(async () => {
    if (!connected) return
    setBusy('Reading board routing')
    setError(undefined)
    try {
      const [provRes, peersRes, linksRes, bleRes] = await Promise.all([
        services.call(PROVIDER_COMMANDS.service, PROVIDER_COMMANDS.list),
        services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.peers),
        services.call('DATA_MATRIX', 'LIST'),
        services.send(bleTableCall(services.catalog)).catch(() => undefined),
      ])
      setLiveSnapshot({
        providers: decodeProviderRows(provRes.data),
        sessions: decodePeers(peersRes.data, services.catalog),
        links: decodeMatrixLinks(linksRes.data),
        gatt: bleRes ? decodeBleTable(link.defs, bleRes.data) : [],
      })
    } catch (e) {
      setError(`Reading board: ${errorText(e)}`)
    } finally {
      setBusy(undefined)
    }
  }, [connected, services, link.defs])

  useEffect(() => {
    if (!connected) {
      setLiveSnapshot(undefined)
      setError(undefined)
      setNote(undefined)
      return
    }
    void refresh()
  }, [connected, refresh])

  const runAction = useCallback(async (label: string, fn: () => Promise<void>, successNote: string): Promise<boolean> => {
    if (!connected) {
      setError('Connect the board first.')
      return false
    }
    setBusy(label)
    setError(undefined)
    setNote(undefined)
    try {
      await fn()
      setNote(successNote)
      await refresh()
      return true
    } catch (e) {
      setError(`${label}: ${errorText(e)}`)
      return false
    } finally {
      setBusy(undefined)
    }
  }, [connected, refresh])

  const createProvider = useCallback(async (id: number, type: string | number, config: FieldValues): Promise<boolean> => {
    const isBle = type === 'ble' || type === services.catalog.providers.get('ble')?.providerType
    return runAction(`Creating provider #${id}`, async () => {
      await services.createProvider(id, type, config)
      if (isBle) {
        // Adding/joining BLE GATT triggers NimBLE Service Changed (~200 ms debounce); give link a brief beat.
        await new Promise((r) => setTimeout(r, 350))
      }
    }, `Created provider #${id}.`)
  }, [runAction, services])

  const removeProvider = useCallback(async (id: number): Promise<boolean> =>
    runAction(`Removing provider #${id}`, async () => {
      await services.call(PROVIDER_COMMANDS.service, PROVIDER_COMMANDS.remove, undefined, { side: 'write', id })
    }, `Removed provider #${id}.`),
  [runAction, services])

  const attachSession = useCallback(async (provider: number, deadmanMs: number, safety: boolean, modules: readonly number[]): Promise<boolean> =>
    runAction(`Attaching session on provider #${provider}`, async () => {
      await services.call('SESSION', 'ATTACH', {
        provider,
        flags: safety ? SESSION_SAFETY_FLAG : 0,
        deadman_ms: deadmanMs,
        modules: Uint8Array.from(modules),
      }, { side: 'write' })
    }, `Attached session on provider #${provider}.`),
  [runAction, services])

  const detachSession = useCallback(async (provider: number): Promise<boolean> =>
    runAction(`Detaching session on provider #${provider}`, async () => {
      await services.call('SESSION', 'DETACH', { provider }, { side: 'write' })
    }, `Detached session on provider #${provider}.`),
  [runAction, services])

  const connectPorts = useCallback(async (from: number, to: number, bothWays = false): Promise<boolean> =>
    runAction('Connecting ports', async () => {
      await services.call('DATA_MATRIX', 'CONNECT', { from, to }, { side: 'write' })
      if (bothWays) {
        await services.call('DATA_MATRIX', 'CONNECT', { from: to, to: from }, { side: 'write' })
      }
    }, bothWays ? 'Connected ports both ways.' : 'Connected ports.'),
  [runAction, services])

  const disconnectPorts = useCallback(async (from: number, to: number): Promise<boolean> =>
    runAction('Disconnecting ports', async () => {
      await services.call('DATA_MATRIX', 'DISCONNECT', { from, to }, { side: 'write' })
    }, 'Disconnected ports.'),
  [runAction, services])

  const clearFeedback = useCallback(() => {
    setError(undefined)
    setNote(undefined)
  }, [])

  return {
    connected,
    live: liveSnapshot !== undefined,
    snapshot: liveSnapshot ?? preview,
    ...(busy ? { busy } : {}),
    ...(error ? { error } : {}),
    ...(note ? { note } : {}),
    services,
    link,
    refresh,
    createProvider,
    removeProvider,
    attachSession,
    detachSession,
    connectPorts,
    disconnectPorts,
    clearFeedback,
  }
}
