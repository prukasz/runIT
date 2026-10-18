import { Check, Loader2, Unplug } from 'lucide-react'
import type { LinkState } from '@backend'
import { Button, Switch } from '@ui/components'
import type { ScanResult } from './desktop'

/** The Connect button while the link is on its way up or up: its text, its colour (styles/status.css) and its icon. Otherwise it reads Connect. */
const CONNECT_VIEW: Partial<Record<LinkState, { label: string; tone: string; Icon: typeof Check }>> = {
  connecting: { label: 'Connecting…', tone: 'is-warn', Icon: Loader2 },
  reconnecting: { label: 'Reconnecting…', tone: 'is-lost', Icon: Loader2 },
  connected: { label: 'Connected', tone: 'is-ok', Icon: Check },
}

/** The right panel when the connection button is open: which board, Connect / Disconnect, the boards nearby (desktop app), connecting automatically, forgetting the board. */
export function ConnectionPanel({ state, device, reason, available, scan, auto, onAuto, sync, onSync, onConnect, onDisconnect, onForget, onChoose, onCancelScan }: {
  state: LinkState
  device?: string
  reason?: string
  available: boolean
  /** The desktop app's scan while its chooser is open (useLink). */
  scan?: readonly ScanResult[]
  auto: boolean
  onAuto: (on: boolean) => void
  /** Read the board into the project each time it connects. */
  sync: boolean
  onSync: (on: boolean) => void
  onConnect: () => void
  onDisconnect: () => void
  onForget: () => void
  onChoose: (id: string) => void
  onCancelScan: () => void
}) {
  const up = state === 'connected' || state === 'reconnecting'
  const down = state === 'idle' || state === 'closed'
  const view = CONNECT_VIEW[state]
  return (
    <div className="connection-panel">
      <div className="connection-board">
        <span className="connection-board-label">My board</span>
        <span className="connection-device">{device ?? 'no board'}</span>
      </div>
      {!available && <p className="connection-error">This browser has no Web Bluetooth. Use Chrome or Edge.</p>}
      {reason && state !== 'connected' && <p className="connection-error">{reason}</p>}
      <div className="connection-actions">
        <Button block className={`connection-state ${view?.tone ?? ''}`} disabled={!available || state === 'connecting' || up || scan !== undefined} onClick={onConnect}>
          {view && <view.Icon aria-hidden="true" className={view.Icon === Loader2 ? 'spinning' : ''} />}
          {view?.label ?? 'Connect'}
        </Button>
        <Button block className={`connection-disconnect ${down ? '' : 'is-armed'}`} disabled={!available || down} onClick={onDisconnect}>
          {!down && <Unplug aria-hidden="true" />}
          {down ? 'Disconnected' : 'Disconnect'}
        </Button>
      </div>
      {scan && (
        <div className="connection-scan">
          <p>{scan.length ? 'Boards nearby' : 'Looking for boards…'}</p>
          {scan.map((found) => (
            <Button block key={found.id} onClick={() => onChoose(found.id)}>{found.name || found.id}</Button>
          ))}
          <Button block variant="dashed" className="connection-cancel" onClick={onCancelScan}>Cancel</Button>
        </div>
      )}
      <Switch checked={auto} onChange={onAuto} label="Connect automatically" />
      <Switch checked={sync} onChange={onSync} label="Read the board when it connects" />
      <div className="connection-actions">
        <Button block disabled={!available || !device} onClick={onForget}>Forget device</Button>
      </div>
    </div>
  )
}
