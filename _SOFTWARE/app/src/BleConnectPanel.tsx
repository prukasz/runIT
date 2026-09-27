import { AlertCircle, CheckCircle2, Loader2, Plug, Unplug, WifiOff } from 'lucide-react'
import type { BleDeviceConnection } from './useBleDeviceConnection'

export interface BleConnectPanelProps {
  connection: BleDeviceConnection
  onApplyProfile?: () => void
  hasUnappliedChanges?: boolean
  /** Settings commands the next apply sends. */
  pendingCommands?: number
  applyBusy?: boolean
  applyMessage?: { readonly ok: boolean; readonly text: string }
  /** The board's running settings are still being read (stored code). */
  applyReading?: boolean
}

export function BleConnectPanel({ connection, onApplyProfile, hasUnappliedChanges, pendingCommands, applyBusy, applyMessage, applyReading }: BleConnectPanelProps) {
  const {
    isConnected,
    isConnecting,
    isAvailable,
    device,
    database,
    status,
    error,
    promptMessage,
    connect,
    disconnect,
    clearPromptMessage,
  } = connection

  return (
    <div className="ble-connect-panel">
      <div className="object-details-header">
        <span className="object-type-icon text" title="BLE Connection">
          <Plug aria-hidden="true" />
        </span>
        <h2>BLE Connection</h2>
        <span className={`ble-conn-badge ${isConnected ? 'connected' : isConnecting ? 'connecting' : 'disconnected'}`}>
          {isConnected ? (
            <>
              <span className="conn-dot online" /> Connected
            </>
          ) : isConnecting ? (
            <>
              <Loader2 className="conn-spin" aria-hidden="true" /> Connecting
            </>
          ) : (
            <>
              <span className="conn-dot offline" /> Disconnected
            </>
          )}
        </span>
      </div>

      {promptMessage && (
        <div className="ble-conn-prompt-alert" role="alert">
          <AlertCircle aria-hidden="true" />
          <div className="prompt-content">
            <strong>Connection Required</strong>
            <p>{promptMessage}</p>
          </div>
          <button
            type="button"
            className="prompt-close-btn"
            onClick={clearPromptMessage}
            aria-label="Dismiss alert"
          >
            ×
          </button>
        </div>
      )}

      {!isAvailable && (
        <div className="ble-conn-warning" role="alert">
          <AlertCircle aria-hidden="true" />
          <p>
            Web Bluetooth is not available in this browser. Please open this app in Google Chrome, Microsoft Edge, or Opera in a secure context (HTTPS / localhost).
          </p>
        </div>
      )}

      {error && (
        <div className="ble-conn-error" role="alert">
          <AlertCircle aria-hidden="true" />
          <span>{error}</span>
        </div>
      )}

      <div className="object-details-section">
        <h3>Device Status</h3>
        {isConnected && device ? (
          <div className="ble-device-info-card">
            <div className="ble-device-row">
              <span className="info-label">Device Name</span>
              <span className="info-value name">{device.name || 'Unnamed Device'}</span>
            </div>
            <div className="ble-device-row">
              <span className="info-label">Device ID</span>
              <span className="info-value id">{device.id}</span>
            </div>
            {database && (
              <div className="ble-device-row">
                <span className="info-label">GATT Services</span>
                <span className="info-value">
                  {database.services.length} services ({database.services.reduce((acc, s) => acc + s.characteristics.length, 0)} chars)
                </span>
              </div>
            )}
          </div>
        ) : (
          <div className="ble-device-empty-card">
            <WifiOff aria-hidden="true" />
            <p>No Bluetooth LE board currently connected.</p>
          </div>
        )}
      </div>

      <div className="object-details-section">
        <h3>Actions</h3>
        <div className="ble-connect-actions-list">
          {!isConnected ? (
            <button
              type="button"
              className="ble-main-connect-btn"
              onClick={() => void connect()}
              disabled={isConnecting || !isAvailable}
            >
              {isConnecting ? (
                <>
                  <Loader2 className="conn-spin" aria-hidden="true" />
                  <span>Connecting...</span>
                </>
              ) : (
                <>
                  <Plug aria-hidden="true" />
                  <span>Connect BLE Device</span>
                </>
              )}
            </button>
          ) : (
            <>
              <button
                type="button"
                className="ble-main-disconnect-btn"
                onClick={() => void disconnect()}
              >
                <Unplug aria-hidden="true" />
                <span>Disconnect</span>
              </button>
              {onApplyProfile && (
                <button
                  type="button"
                  className={`ble-conn-apply-btn ${hasUnappliedChanges ? 'has-changes' : ''}`}
                  onClick={onApplyProfile}
                  disabled={applyBusy || applyReading}
                  title="Send the BLE and connector changes to the board"
                >
                  <CheckCircle2 aria-hidden="true" />
                  <span>{applyBusy ? 'Applying…' : applyReading ? 'Reading board…' : hasUnappliedChanges ? `Apply ${pendingCommands ? `${pendingCommands} ` : ''}Change${pendingCommands === 1 ? '' : 's'}` : 'Settings Applied'}</span>
                </button>
              )}
            </>
          )}
        </div>
        <p className="ble-connect-tip">
          {isConnected
            ? 'Device is online and ready. GATT changes can be applied via the top action bar green tickmark.'
            : 'Click "Connect BLE Device" to open the browser Bluetooth picker and select your runIT peripheral.'}
        </p>
      </div>

      {applyMessage && (
        <div className={`ble-conn-status-bar ${applyMessage.ok ? '' : 'is-error'}`}>
          <span className="status-label">Apply:</span>
          <span className="status-text">{applyMessage.text}</span>
        </div>
      )}

      {status && (
        <div className="ble-conn-status-bar">
          <span className="status-label">Log:</span>
          <span className="status-text">{status}</span>
        </div>
      )}
    </div>
  )
}
