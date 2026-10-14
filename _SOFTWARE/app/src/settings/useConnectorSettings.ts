import { useCallback, useMemo, useState } from 'react'
import { enumMembers } from '../backend/catalog'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { boardConnectors, boardSetup, connectorsFromJson, connectorsModified, hex, restoreBinding, restoreConnector, restoreSystemConnectors } from '../domain/settings'
import type { ConnectorBindingSettings, ConnectorSettings } from '../domain/settings'

/** Auto-saved on every change, like the object tree. */
const STORAGE_KEY = 'runit.settings.connectors'

/** Where the editor starts looking for a free stream byte for a user connector (the examples in SYS_CONNECTOR.MD use 0x10). */
const FIRST_USER_STREAM = 0x10

/**
 * The data connectors: the board's four system streams (logs, errors, telemetry, events: they stay, their bindings and pause can
 * change) and the user's own, created at runtime by the `conn` router.
 */
export function useConnectorSettings() {
  const [connectors, setConnectors] = useState<ConnectorSettings[]>(() => readStored(STORAGE_KEY, connectorsFromJson, boardConnectors()))
  const [selectedKey, setSelectedKey] = useState('telemetry')

  usePersistEffect(STORAGE_KEY, connectors)

  const select = useCallback((key: string) => setSelectedKey(key), [])

  const selectedConnector = useMemo(() => connectors.find((connector) => connector.key === selectedKey) ?? connectors[0], [connectors, selectedKey])

  const addConnector = useCallback(() => {
    setConnectors((current) => {
      const { appBase, limits } = boardSetup()
      const ids = new Set(current.map((connector) => connector.id))
      let id = appBase
      while (ids.has(id)) id++
      if (id >= limits.connectorsMax) return current
      // The stream bytes the firmware owns (telemetry, logs, errors, the router's answers, events) and the ones already used.
      const taken = new Set([...enumMembers('stream_byte').map((entry) => entry.value), ...current.map((connector) => Number.parseInt(connector.stream, 16))])
      let stream = FIRST_USER_STREAM
      while (taken.has(stream) && stream < 0xff) stream++
      const key = `connector_${id}`
      setSelectedKey(key)
      return [...current, { id, key, name: `stream_${id}`, alias: `stream_${id}`, stream: hex(stream, 2), system: false, description: '', isSuspended: false, bindings: [] }]
    })
  }, [])

  const updateConnector = useCallback((key: string, patch: Partial<ConnectorSettings>) => setConnectors((current) => current.map((connector) => (connector.key === key ? { ...connector, ...patch } : connector))), [])

  const deleteConnector = useCallback((key: string) => {
    setConnectors((current) => {
      if (current.find((connector) => connector.key === key)?.system) return current
      const next = current.filter((connector) => connector.key !== key)
      if (selectedKey === key && next.length) setSelectedKey(next[0]!.key)
      return next
    })
  }, [selectedKey])

  /** A connector has one binding per transport: binding that transport again re-addresses it (the router's BIND does the same). */
  const addBinding = useCallback((key: string, binding: Omit<ConnectorBindingSettings, 'id'>) => {
    setConnectors((current) => current.map((connector) => {
      if (connector.key !== key) return connector
      const next: ConnectorBindingSettings = { id: `b_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`, ...binding }
      return { ...connector, bindings: [...connector.bindings.filter((entry) => entry.provider !== binding.provider), next] }
    }))
  }, [])

  const removeBinding = useCallback((key: string, bindingId: string) => setConnectors((current) => current.map((connector) => (connector.key === key ? { ...connector, bindings: connector.bindings.filter((binding) => binding.id !== bindingId) } : connector))), [])

  const toggleSuspend = useCallback((key: string) => setConnectors((current) => current.map((connector) => (connector.key === key ? { ...connector, isSuspended: !connector.isSuspended } : connector))), [])

  // What was removed from a system connector can be put back from the board's own definition.
  const restoreOne = useCallback((key: string) => setConnectors((current) => restoreConnector(current, key)), [])
  const restoreOneBinding = useCallback((key: string, bindingId: string) => setConnectors((current) => restoreBinding(current, key, bindingId)), [])
  const restoreAll = useCallback(() => setConnectors((current) => restoreSystemConnectors(current)), [])
  const modified = useMemo(() => connectorsModified(connectors), [connectors])

  const replaceConnectors = useCallback((next: ConnectorSettings[]) => setConnectors(next), [])

  return { replaceConnectors, connectors, selectedKey, selectedConnector, select, addConnector, updateConnector, deleteConnector, addBinding, removeBinding, toggleSuspend, restoreConnector: restoreOne, restoreBinding: restoreOneBinding, restoreAll, modified }
}

export type ConnectorSettingsState = ReturnType<typeof useConnectorSettings>
