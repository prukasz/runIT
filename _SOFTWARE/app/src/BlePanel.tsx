import { useState } from 'react'
import { Switch } from './components/Switch/Switch'
import type { LinkState, RunitBleLink } from './ble'

const CONNECT_LABEL: Partial<Record<LinkState, string>> = {
  connecting: 'Connecting…',
  waiting: 'Reconnecting…',
  connected: 'Connected',
}
const DISCONNECT_LABEL: Partial<Record<LinkState, string>> = {
  idle: 'Disconnected',
}

/** Connect, disconnect and automatic reconnect for the board link. */
export function BlePanel({ link, state, device, auto, onAuto }: {
  link: RunitBleLink
  state: LinkState
  device: string | undefined
  auto: boolean
  onAuto: (on: boolean) => void
}) {
  const [error, setError] = useState<string | null>(null)
  const run = async (action: () => Promise<unknown>) => {
    setError(null)
    try { await action() } catch (e) { setError(e instanceof Error ? e.message : String(e)) }
  }
  const up = state === 'connected' || state === 'waiting'

  return (
    <div className="ble-panel">
      <div className="ble-board">
        <span className="ble-board-label">My board</span>
        <span className="ble-device">{device ?? 'no board'}</span>
      </div>
      {error && <p className="ble-error">{error}</p>}
      <div className="ble-actions">
        <button className="text-button" disabled={state === 'unavailable' || state === 'connecting' || up}
          onClick={() => run(async () => { if (!(await link.restore())) await link.pick() })}>
          {CONNECT_LABEL[state] ?? 'Connect'}
        </button>
        <button className="text-button" disabled={state === 'idle' || state === 'unavailable'} onClick={() => run(() => link.disconnect())}>
          {DISCONNECT_LABEL[state] ?? 'Disconnect'}
        </button>
      </div>
      <Switch checked={auto} onChange={onAuto} label="Connect automatically" />
      <div className="ble-actions">
        <button className="text-button" onClick={() => run(() => link.forget())}>
          Forget device
        </button>
      </div>
    </div>
  )
}
