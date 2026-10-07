import { useCallback, useState } from 'react'
import { Zap } from 'lucide-react'
import { readStored, usePersistEffect } from './hooks/useStorage'
import { DEFAULT_POWER } from './domain/project'
import type { PowerSettings } from './domain/project'

/** Auto-saved on every change, like the other settings; also saved in the project file. */
const STORAGE_KEY = 'runit.power'

const parsePower = (raw: string): PowerSettings | undefined => {
  const { capsDisabled } = JSON.parse(raw) as Partial<PowerSettings>
  return typeof capsDisabled === 'boolean' ? { capsDisabled } : undefined
}

export interface PowerSettingsWorkspace {
  readonly power: PowerSettings
  readonly update: (patch: Partial<PowerSettings>) => void
  readonly load: (next: PowerSettings) => void
}

export function usePowerSettingsWorkspace(): PowerSettingsWorkspace {
  const [power, setPower] = useState<PowerSettings>(() => readStored(STORAGE_KEY, parsePower, DEFAULT_POWER))
  usePersistEffect(STORAGE_KEY, power)
  const update = useCallback((patch: Partial<PowerSettings>) => setPower((prev) => ({ ...prev, ...patch })), [])
  const load = useCallback((next: PowerSettings) => setPower({ ...next }), [])
  return { power, update, load }
}

export function PowerSettingsEditor({ workspace, connected }: { readonly workspace: PowerSettingsWorkspace; readonly connected: boolean }) {
  const { power, update } = workspace
  return (
    <div className="object-editor">
      <div className="ble-general-header">
        <Zap aria-hidden="true" />
        <h1>Power</h1>
      </div>
      <p>How the board limits its outputs. These settings are sent to the board each time it connects and whenever you change them; the board keeps them across restarts.</p>
      <div className="object-details-toggles">
        <label className="object-details-check">
          <input type="checkbox" checked={power.capsDisabled} onChange={(event) => update({ capsDisabled: event.target.checked })} />
          <span className="object-check-box" aria-hidden="true" />
          <span>Disable power caps</span>
        </label>
      </div>
      <p className="program-muted">
        With the caps on, the board shares its power budget between the outputs and refuses a setting that doesn't fit. With them off there is no
        budget and no current cap from the source; only the board's 20 V limit stays. {connected ? 'Connected: changes are sent right away.' : 'Not connected: sent when a board connects.'}
      </p>
    </div>
  )
}
