import { useCallback, useState } from 'react'
import { ScrollText } from 'lucide-react'
import { FormGrid, FormRow, SelectField } from './components/FormField'
import { readStored, usePersistEffect } from './hooks/useStorage'
import type { LogSettings } from './domain/project'
import { DEFAULT_LOGS, LOG_LEVEL_MAX } from './domain/upload'

/** Auto-saved on every change, like the other settings; also saved in the project file. */
const STORAGE_KEY = 'runit.logs'

/** esp_log_level_t, in the order of its values. */
const LEVELS = ['None', 'Error', 'Warning', 'Info', 'Debug', 'Verbose'] as const

const parseLogs = (raw: string): LogSettings | undefined => {
  const value = JSON.parse(raw) as Partial<LogSettings>
  const { level, mirrorSerial, traceErrors } = value
  if (typeof level !== 'number' || !Number.isInteger(level) || level < 0 || level > LOG_LEVEL_MAX) return undefined
  if (typeof mirrorSerial !== 'boolean' || typeof traceErrors !== 'boolean') return undefined
  return { level, mirrorSerial, traceErrors }
}

export interface LogSettingsWorkspace {
  readonly logs: LogSettings
  readonly update: (patch: Partial<LogSettings>) => void
  readonly load: (next: LogSettings) => void
}

export function useLogSettingsWorkspace(): LogSettingsWorkspace {
  const [logs, setLogs] = useState<LogSettings>(() => readStored(STORAGE_KEY, parseLogs, DEFAULT_LOGS))
  usePersistEffect(STORAGE_KEY, logs)
  const update = useCallback((patch: Partial<LogSettings>) => setLogs((prev) => ({ ...prev, ...patch })), [])
  const load = useCallback((next: LogSettings) => setLogs({ ...next }), [])
  return { logs, update, load }
}

export function LogSettingsEditor({ workspace }: { readonly workspace: LogSettingsWorkspace }) {
  const { logs, update } = workspace
  return (
    <div className="object-editor">
      <div className="ble-general-header">
        <ScrollText aria-hidden="true" />
        <h1>Logging</h1>
      </div>
      <p>What the board logs while it runs. Sent to the board with the other settings (Apply) and kept in its stored code.</p>
      <FormGrid>
        <FormRow label="Log level">
          <SelectField className="form-field-panel" value={logs.level} onChange={(event) => update({ level: Number(event.target.value) })}>
            {LEVELS.map((name, level) => <option key={name} value={level}>{name}</option>)}
          </SelectField>
        </FormRow>
        <FormRow label="Mirror to serial">
          <input type="checkbox" checked={logs.mirrorSerial} onChange={(event) => update({ mirrorSerial: event.target.checked })} />
        </FormRow>
        <FormRow label="Trace error chains">
          <input type="checkbox" checked={logs.traceErrors} onChange={(event) => update({ traceErrors: event.target.checked })} />
        </FormRow>
      </FormGrid>
      <p className="program-muted">
        The level applies to every log tag. Serial mirror also prints the logs on the board's UART console; error chains log where an
        error came from. Firmware default: Info, mirrored, traced.
      </p>
    </div>
  )
}
