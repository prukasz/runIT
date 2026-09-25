import { useEffect, useRef, useState } from 'react'
import { shortBleUuid, WebBluetoothAdapter } from './backend/ble'
import type { BleCharacteristic, BleDevice, BleGattDatabase, BleUuid } from './backend/ble'
import { openRunitBleSession } from './backend/runitBleSession'
import type { RunitBleSession } from './backend/runitBleSession'
import CommandConsole from './CommandConsole'
import DiagnosticsConsole from './DiagnosticsConsole'
import { runitInterfaceProtocol, runitStreamCatalog } from './domain/descriptors'

const STREAMS = runitStreamCatalog()
const RUNIT_SERVICE_UUID = STREAMS.ble.service
const SESSION_CHARACTERISTICS = new Set(STREAMS.ble.notify.map((uuid) => shortBleUuid(uuid)))
const DECODED_STREAMS = new Set(['logs', 'errors'])

/** What the board uses a characteristic for, from its connector bindings (streams.generated.json). */
const runitRole = (uuid: BleUuid): string | undefined => {
  const short = shortBleUuid(uuid)
  if (short === undefined) return undefined
  if (short === shortBleUuid(STREAMS.ble.service)) return 'runIT service'
  const uses = (key: 'notify' | 'write') => STREAMS.streams.filter((stream) => stream.ble?.[key] !== undefined && shortBleUuid(stream.ble[key]) === short).map((stream) => stream.name)
  const parts = [uses('notify').length ? `notify: ${uses('notify').join(', ')}` : '', uses('write').length ? `write: ${uses('write').join(', ')}` : ''].filter(Boolean)
  return parts.length ? `runIT ${parts.join(' · ')}` : undefined
}

const hexToBytes = (value: string): Uint8Array => {
  const clean = value.replaceAll('0x', '').replaceAll(/\s/g, '')
  if (!clean || clean.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(clean)) {
    throw new Error('Enter complete hexadecimal bytes, for example: 04 48 07')
  }
  return Uint8Array.from(clean.match(/../g)!, (byte) => Number.parseInt(byte, 16))
}

const formatHex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0').toUpperCase()).join(' ')

const describe = (characteristic: BleCharacteristic): string =>
  `${runitRole(characteristic.uuid) ?? 'Characteristic'}${shortBleUuid(characteristic.uuid) ? ` · ${shortBleUuid(characteristic.uuid)}` : ''} · ${characteristic.properties.join(', ') || 'no properties'}`

export default function BleTestApp() {
  const adapterRef = useRef<WebBluetoothAdapter | null>(null)
  if (!adapterRef.current) adapterRef.current = new WebBluetoothAdapter()
  const adapter = adapterRef.current

  const subscriptions = useRef(new Map<string, () => Promise<void>>())
  const [device, setDevice] = useState<BleDevice>()
  const [database, setDatabase] = useState<BleGattDatabase>()
  const [connectionState, setConnectionState] = useState(adapter.getConnectionState())
  const [status, setStatus] = useState('Ready. Select a runIT board to begin.')
  const [busy, setBusy] = useState(false)
  const [notifications, setNotifications] = useState<string[]>([])
  const [writeValues, setWriteValues] = useState<Record<string, string>>({})
  const [writeWithResponse, setWriteWithResponse] = useState(true)
  const [subscribed, setSubscribed] = useState<ReadonlySet<string>>(new Set())
  const [session, setSession] = useState<RunitBleSession>()
  const [rawDecoded, setRawDecoded] = useState(false)

  useEffect(() => {
    if (!session) return undefined
    const stopFrames = session.received.subscribe((frame) => {
      const stream = STREAMS.byHeader(frame.data[0])
      // Logs and errors have their own panels; keep them out of the raw log unless asked.
      if (!rawDecoded && stream && DECODED_STREAMS.has(stream.name)) return
      setNotifications((entries) => [`${frame.route} ${stream?.name ?? 'unknown stream'}: ${formatHex(frame.data)}`, ...entries].slice(0, 100))
    })
    return () => stopFrames()
  }, [session, rawDecoded])

  useEffect(() => {
    if (!session) return undefined
    return () => void session.close()
  }, [session])

  useEffect(() => {
    const stopDisconnect = adapter.onDisconnect((disconnected) => {
      subscriptions.current.clear()
      setSession(undefined)
      setSubscribed(new Set())
      setDatabase(undefined)
      setDevice(undefined)
      setStatus(`${disconnected.name ?? disconnected.id} disconnected.`)
    })
    const stopState = adapter.onStateChange(setConnectionState)
    return () => {
      stopDisconnect()
      stopState()
    }
  }, [adapter])

  const run = async (label: string, operation: () => Promise<void>): Promise<void> => {
    setBusy(true)
    try {
      await operation()
      if (label) setStatus(label)
    } catch (error) {
      setStatus(error instanceof Error ? error.message : String(error))
    } finally {
      setBusy(false)
    }
  }

  const selectDevice = (): Promise<void> => run('Device selected. Connect when ready.', async () => {
    const selected = await adapter.requestDevice({
      filters: [{ namePrefix: 'runit' }, { serviceUuids: [RUNIT_SERVICE_UUID] }],
      optionalServiceUuids: [RUNIT_SERVICE_UUID],
    })
    setDevice(selected)
    setDatabase(undefined)
  })

  const connect = (): Promise<void> => run('Connected. Rediscover GATT services to inspect characteristics.', async () => {
    if (!device) throw new Error('Select a board first.')
    await adapter.connect(device)
  })

  const rediscover = (): Promise<void> => run('', async () => {
    setSession(undefined)
    await Promise.all([...subscriptions.current.values()].map((unsubscribe) => unsubscribe()))
    subscriptions.current.clear()
    setSubscribed(new Set())
    const discovered = await adapter.discover()
    setDatabase(discovered)
    const mtu = await adapter.getMtu()
    const mtuText = mtu.value === null ? 'MTU is negotiated by Windows/Linux but hidden from Web Bluetooth.' : `MTU: ${mtu.value}.`
    try {
      setSession(await openRunitBleSession(adapter, { layout: STREAMS.ble, protocol: runitInterfaceProtocol() }))
      setStatus(`GATT database rediscovered, command session open. ${mtuText}`)
    } catch (error) {
      setStatus(`GATT database rediscovered, no command session (${error instanceof Error ? error.message : String(error)}). ${mtuText}`)
    }
  })

  const disconnect = (): Promise<void> => run('Disconnected.', async () => {
    setSession(undefined)
    await Promise.all([...subscriptions.current.values()].map((unsubscribe) => unsubscribe()))
    subscriptions.current.clear()
    await adapter.disconnect()
    setSubscribed(new Set())
    setDatabase(undefined)
  })

  const read = (characteristic: BleCharacteristic): Promise<void> => run(`Read ${characteristic.uuid}.`, async () => {
    const data = await adapter.read(characteristic)
    setNotifications((entries) => [`RX ${characteristic.uuid}: ${formatHex(data)}`, ...entries].slice(0, 100))
  })

  const toggleSubscription = (characteristic: BleCharacteristic): Promise<void> => run('', async () => {
    const unsubscribe = subscriptions.current.get(characteristic.id)
    if (unsubscribe) {
      await unsubscribe()
      subscriptions.current.delete(characteristic.id)
      setSubscribed(new Set(subscriptions.current.keys()))
      setStatus(`Stopped notifications from ${characteristic.uuid}.`)
      return
    }
    const stop = await adapter.subscribe(characteristic, (data) => {
      setNotifications((entries) => [`NTF ${characteristic.uuid}: ${formatHex(data)}`, ...entries].slice(0, 100))
    })
    subscriptions.current.set(characteristic.id, stop)
    setSubscribed(new Set(subscriptions.current.keys()))
    setStatus(`Listening on ${characteristic.uuid}.`)
  })

  const write = (characteristic: BleCharacteristic): Promise<void> => run(`Wrote ${characteristic.uuid}.`, async () => {
    await adapter.write(characteristic, hexToBytes(writeValues[characteristic.id] ?? ''), writeWithResponse)
  })

  const capabilities = adapter.getCapabilities()
  const diagnostics = adapter.getDiagnostics()
  const services = database?.services ?? []

  return (
    <main className="min-h-screen bg-slate-950 p-4 font-mono text-sm text-slate-200 sm:p-8">
      <section className="mx-auto max-w-5xl space-y-4">
        <header className="border-b border-slate-700 pb-4">
          <h1 className="text-lg font-semibold text-white">runIT BLE transport test</h1>
          <p className="mt-1 text-slate-400">Raw GATT inspection plus a command console (descriptor-built packets, answers matched by seq).</p>
        </header>

        <section className="rounded border border-slate-700 bg-slate-900 p-4">
          <div className="flex flex-wrap gap-2">
            <button onClick={() => void selectDevice()} disabled={busy || !capabilities.interactiveDeviceSelection.available}>Select board</button>
            <button onClick={() => void connect()} disabled={busy || !device || adapter.isConnected()}>Connect</button>
            <button onClick={() => void rediscover()} disabled={busy || !adapter.isConnected()}>Rediscover GATT</button>
            <button onClick={() => void disconnect()} disabled={busy || !adapter.isConnected()}>Disconnect</button>
          </div>
          <p className="mt-3 text-slate-300">{status}</p>
          <p className="mt-1 text-xs text-emerald-300">State: {connectionState} · connections: {diagnostics.connectCount} · discoveries: {diagnostics.discoveryCount} · RX: {diagnostics.bytesRead} B / {diagnostics.notificationCount} notifications · TX: {diagnostics.bytesWritten} B / {diagnostics.writeCount} writes</p>
          <dl className="mt-3 grid gap-1 text-xs text-slate-400 sm:grid-cols-2">
            <div><dt className="inline text-slate-500">Device: </dt><dd className="inline">{device ? `${device.name ?? 'unnamed'} (${device.id})` : 'none'}</dd></div>
            <div><dt className="inline text-slate-500">Pairing: </dt><dd className="inline">{capabilities.pairing.reason ?? 'available'}</dd></div>
            <div><dt className="inline text-slate-500">Passkey: </dt><dd className="inline">{capabilities.passkeyEntry.reason ?? 'available'}</dd></div>
            <div><dt className="inline text-slate-500">MTU: </dt><dd className="inline">{capabilities.mtu.reason ?? 'available'}</dd></div>
          </dl>
        </section>

        <label className="flex items-center gap-2 text-xs text-slate-400">
          <input type="checkbox" checked={writeWithResponse} onChange={(event) => setWriteWithResponse(event.target.checked)} />
          Write with response
        </label>

        <section className="space-y-3">
          {services.map((service) => (
            <article key={service.uuid} className="rounded border border-slate-700 bg-slate-900 p-4">
              <h2 className="font-semibold text-sky-300">{runitRole(service.uuid) ?? 'Service'}{shortBleUuid(service.uuid) ? ` · ${shortBleUuid(service.uuid)}` : ''}</h2>
              <p className="mt-1 text-xs text-slate-500">Full UUID: {String(service.uuid)}</p>
              <div className="mt-3 space-y-3">
                {service.characteristics.map((characteristic) => {
                  const canRead = characteristic.properties.includes('read')
                  const canSubscribe = characteristic.properties.includes('notify') || characteristic.properties.includes('indicate')
                  const heldBySession = session !== undefined && SESSION_CHARACTERISTICS.has(shortBleUuid(characteristic.uuid))
                  const canWrite = characteristic.properties.includes('write') || characteristic.properties.includes('write-without-response')
                  return (
                    <div key={characteristic.id} className="rounded border border-slate-800 bg-slate-950 p-3">
                      <p className="text-slate-300">{describe(characteristic)}</p>
                      <p className="mt-1 text-xs text-slate-500">Full UUID: {String(characteristic.uuid)}</p>
                      {characteristic.descriptors.map((descriptor) => (
                        <p key={`${characteristic.id}-${String(descriptor.uuid)}`} className="mt-1 text-xs text-violet-300">
                          Descriptor {String(descriptor.uuid)} · {descriptor.name} ({descriptor.discovery}; controlled by Subscribe)
                        </p>
                      ))}
                      <div className="mt-2 flex flex-wrap gap-2">
                        {canRead && <button onClick={() => void read(characteristic)} disabled={busy}>Read</button>}
                        {canSubscribe && !heldBySession && <button onClick={() => void toggleSubscription(characteristic)} disabled={busy}>{subscribed.has(characteristic.id) ? 'Unsubscribe' : 'Subscribe'}</button>}
                        {heldBySession && <span className="text-xs text-emerald-400">Subscribed by the command session</span>}
                        {canWrite && <>
                          <input aria-label={`Hex bytes for ${characteristic.uuid}`} value={writeValues[characteristic.id] ?? ''} onChange={(event) => setWriteValues((values) => ({ ...values, [characteristic.id]: event.target.value }))} placeholder="04 48 07" />
                          <button onClick={() => void write(characteristic)} disabled={busy}>Write hex</button>
                        </>}
                      </div>
                    </div>
                  )
                })}
              </div>
            </article>
          ))}
          {adapter.isConnected() && services.length === 0 && <p className="text-slate-500">No services loaded. Press “Rediscover GATT”.</p>}
        </section>

        <CommandConsole session={session} />

        <DiagnosticsConsole session={session} />

        <section className="rounded border border-slate-700 bg-slate-900 p-4">
          <h2 className="font-semibold text-white">Received data (raw)</h2>
          <div className="mt-2 flex flex-wrap items-center gap-3">
            <button onClick={() => setNotifications([])}>Clear</button>
            <label className="flex items-center gap-1 text-xs text-slate-400">
              <input type="checkbox" checked={rawDecoded} onChange={(event) => setRawDecoded(event.target.checked)} />
              Include logs and errors frames
            </label>
          </div>
          <pre className="mt-3 max-h-64 overflow-auto whitespace-pre-wrap text-xs text-emerald-300">{notifications.join('\n') || 'No reads or notifications yet.'}</pre>
        </section>
      </section>
    </main>
  )
}
