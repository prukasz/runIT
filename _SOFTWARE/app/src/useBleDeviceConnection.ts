import { useEffect, useRef, useState, useCallback } from 'react'
import { WebBluetoothAdapter } from './backend/ble'
import type { BleDevice, BleConnectionState, BleGattDatabase } from './backend/ble'
import { runitStreamCatalog, runitInterfaceProtocol } from './domain/descriptors'
import { openRunitBleSession } from './backend/runitBleSession'
import type { RunitBleSession } from './backend/runitBleSession'

export interface BleDeviceConnection {
  adapter: WebBluetoothAdapter
  connectionState: BleConnectionState
  isConnected: boolean
  isConnecting: boolean
  isAvailable: boolean
  device: BleDevice | undefined
  database: BleGattDatabase | undefined
  /** The runIT command channel; undefined until connected (or when the board isn't a runIT one). */
  session: RunitBleSession | undefined
  status: string
  error: string | null
  promptMessage: string | null
  connect: () => Promise<boolean>
  disconnect: () => Promise<void>
  notifyDisconnectedAttempt: () => void
  clearPromptMessage: () => void
}

/** Bluetooth SIG services the picker also asks access to: Generic Access, Generic Attribute, Device Information. */
const STANDARD_GATT_SERVICES = [0x1800, 0x1801, 0x180a] as const

/**
 * `extraServices`: 16-bit UUIDs of services the app configures on the board
 * (user services from the BLE settings). Web Bluetooth only exposes services
 * named when the device is picked, so they must be listed at connect.
 */
export function useBleDeviceConnection(extraServices: readonly number[] = []): BleDeviceConnection {
  const adapterRef = useRef<WebBluetoothAdapter | null>(null)
  if (!adapterRef.current) {
    adapterRef.current = new WebBluetoothAdapter()
  }
  const adapter = adapterRef.current

  const [connectionState, setConnectionState] = useState<BleConnectionState>(() => adapter.getConnectionState())
  const [device, setDevice] = useState<BleDevice | undefined>(() => adapter.getConnectedDevice())
  const [database, setDatabase] = useState<BleGattDatabase | undefined>(() => adapter.getGattDatabase())
  const [status, setStatus] = useState<string>('Ready to connect')
  const [error, setError] = useState<string | null>(null)
  const [promptMessage, setPromptMessage] = useState<string | null>(null)
  const sessionRef = useRef<RunitBleSession | null>(null)
  const [session, setSession] = useState<RunitBleSession | undefined>()

  const isAvailable = typeof navigator !== 'undefined' && adapter.isAvailable()
  const isConnected = connectionState === 'connected' && adapter.isConnected()
  const isConnecting = connectionState === 'connecting'

  useEffect(() => {
    const stopDisconnect = adapter.onDisconnect((disconnected) => {
      setDevice(undefined)
      setDatabase(undefined)
      if (sessionRef.current) {
        void sessionRef.current.close()
        sessionRef.current = null
      }
      setSession(undefined)
      setStatus(`${disconnected.name || 'Device'} disconnected.`)
    })

    const stopState = adapter.onStateChange((state) => {
      setConnectionState(state)
      if (state === 'connected') {
        const dev = adapter.getConnectedDevice()
        setDevice(dev)
        setStatus(`Connected to ${dev?.name || 'runIT device'}`)
        setError(null)
      } else if (state === 'disconnected') {
        setDevice(undefined)
        setDatabase(undefined)
      }
    })

    return () => {
      stopDisconnect()
      stopState()
    }
  }, [adapter])

  const connect = useCallback(async (): Promise<boolean> => {
    if (!isAvailable) {
      const err = 'Web Bluetooth is not available in this browser. Please use Chrome, Edge, or Opera in a secure context.'
      setError(err)
      setStatus(err)
      return false
    }

    setError(null)
    setStatus('Selecting BLE device...')
    setPromptMessage(null)

    try {
      const streams = runitStreamCatalog()

      // Request device with optional services so GATT access is granted: the runIT service (descriptors), user services, standard ones.
      const selected = await adapter.requestDevice({
        acceptAllDevices: true,
        optionalServiceUuids: [...new Set([streams.ble.service, ...extraServices, ...STANDARD_GATT_SERVICES])],
      })

      setStatus(`Connecting to ${selected.name || 'device'}...`)
      await adapter.connect(selected)

      setStatus('Discovering GATT services & characteristics...')
      const gattDb = await adapter.discover()
      setDatabase(gattDb)

      try {
        const opened = await openRunitBleSession(adapter, {
          layout: streams.ble,
          protocol: runitInterfaceProtocol(),
        })
        sessionRef.current = opened
        setSession(opened)
        setStatus(`Connected to ${selected.name || 'runIT device'} (GATT active)`)
      } catch (sessErr) {
        // Connected to the GATT server, but it has no runIT command channel: nothing can be uploaded.
        setStatus(`Connected to ${selected.name || 'device'} (GATT discovered, no runIT channel: ${sessErr instanceof Error ? sessErr.message : String(sessErr)})`)
      }

      setDevice(selected)
      return true
    } catch (err: unknown) {
      if (err instanceof Error) {
        if (err.name === 'NotFoundError') {
          setStatus('Device selection cancelled.')
          return false
        }
        setError(err.message)
        setStatus(`Connection failed: ${err.message}`)
      } else {
        setError(String(err))
        setStatus('Connection failed.')
      }
      return false
    }
  }, [adapter, isAvailable, extraServices])

  const disconnect = useCallback(async (): Promise<void> => {
    try {
      setStatus('Disconnecting...')
      if (sessionRef.current) {
        await sessionRef.current.close()
        sessionRef.current = null
      }
      setSession(undefined)
      await adapter.disconnect()
      setStatus('Disconnected.')
      setDevice(undefined)
      setDatabase(undefined)
    } catch (err: unknown) {
      setStatus(err instanceof Error ? err.message : 'Error disconnecting')
    }
  }, [adapter])

  const notifyDisconnectedAttempt = useCallback(() => {
    setPromptMessage('BLE device is not connected. Connect your board to apply configuration changes.')
  }, [])

  const clearPromptMessage = useCallback(() => {
    setPromptMessage(null)
  }, [])

  return {
    adapter,
    connectionState,
    isConnected,
    isConnecting,
    isAvailable,
    device,
    database,
    session,
    status,
    error,
    promptMessage,
    connect,
    disconnect,
    notifyDisconnectedAttempt,
    clearPromptMessage,
  }
}
