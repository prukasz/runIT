import { Trash2 } from 'lucide-react'
import { runitErrorCatalog } from '../domain/descriptors'
import { clampErrorActions, decodeErrorActions, encodeErrorActions, ERROR_ACTION_LEVELS, findContract, initialParameterValue, reachableErrorLevels, SET_ERROR_HANDLING } from '../domain/devices'
import type { ErrorAction, ResolvedDevice } from '../domain/devices'
import type { ProjectAction } from '../domain/project'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import { runitDeviceCatalog } from '../domain/descriptors'

/*
 * A device's error handling (sys_device_set_error_handling): its importance,
 * which selects the minimum severity to handle (Disabled ignores noncritical
 * errors; Critical errors always go through), and the action per level: none, a
 * built-in static action or one of the project's recorded actions.
 */

const levelName = (level: number): string => runitErrorCatalog().level(level)?.alias ?? `Level ${level}`
const optionOf = (action: ErrorAction): string => (action.id ? `${action.scope === 'dynamic' ? 'd' : 's'}:${action.id}` : '')
const actionOf = (option: string): ErrorAction => (option ? { scope: option.startsWith('d:') ? 'dynamic' : 'static', id: Number(option.slice(2)) } : { scope: 'static', id: 0 })

/** One action picker per error level the importance can reach, over the wire array `[scope bits, Low, Medium, High, Critical]`. */
export function ErrorActionsField({ value, actions, importance, onChange }: { value: readonly number[]; actions: readonly ProjectAction[]; importance: number; onChange: (value: number[]) => void }) {
  const reachable = reachableErrorLevels(importance)
  const current = decodeErrorActions(value)
  const staticActions = runitDeviceCatalog().staticActions
  const set = (index: number, option: string) => onChange(encodeErrorActions(current.map((entry, at) => (at === index ? actionOf(option) : entry))))
  return (
    <div className="devices-error-actions">
      {ERROR_ACTION_LEVELS.map((level, index) => {
        if (!reachable.includes(level)) return null
        const action = current[index]!
        const option = optionOf(action)
        const known = !action.id || (action.scope === 'dynamic' ? actions.some((entry) => entry.actionId === action.id) : staticActions.some((entry) => entry.value === action.id))
        return (
          <label key={level} className="devices-error-action">
            <span>{levelName(level)}</span>
            <select value={option} onChange={(event) => set(index, event.target.value)}>
              <option value="">No action</option>
              {!known && <option value={option}>{action.scope === 'dynamic' ? 'Recorded' : 'Static'} action {action.id} (not in this project)</option>}
              <optgroup label="Built-in">
                {staticActions.map((entry) => <option key={entry.value} value={`s:${entry.value}`} title={entry.description}>{entry.label}</option>)}
              </optgroup>
              {actions.length > 0 && (
                <optgroup label="Actions">
                  {actions.map((entry) => <option key={entry.id} value={`d:${entry.actionId}`}>{entry.name} (ID {entry.actionId})</option>)}
                </optgroup>
              )}
            </select>
          </label>
        )
      })}
    </div>
  )
}

/** The device page card: importance and per-level actions, saved as a default setting run at every boot. */
export function ErrorHandlingCard({ w, device }: { w: DevicesWorkspace; device: ResolvedDevice }) {
  const contract = findContract(w.catalog, device, SET_ERROR_HANDLING)
  if (!contract) return null
  const step = w.setup.find((entry) => entry.device === device.ref && entry.contract === SET_ERROR_HANDLING)
  const importanceParameter = contract.parameters.find((parameter) => parameter.name === 'importance')
  const actionsParameter = contract.parameters.find((parameter) => parameter.name === 'actions')
  // A new device starts at 0 (calloc in sys_device.c): Disabled until set.
  const importance = typeof step?.values.importance === 'number' ? step.values.importance : 0
  const actions = Array.isArray(step?.values.actions) ? step.values.actions : actionsParameter ? (initialParameterValue(actionsParameter) as number[]) : [0, 0, 0, 0, 0]
  const choices = importanceParameter?.choices ?? w.catalog.importance
  const save = (patch: { importance?: number; actions?: number[] }) => {
    const nextImportance = patch.importance ?? importance
    const values = { importance: nextImportance, actions: clampErrorActions(patch.actions ?? actions, nextImportance) }
    if (step) w.updateSetup(step.id, { values })
    else w.addSetup(device.ref, SET_ERROR_HANDLING, values)
  }
  const current = choices.find((choice) => choice.value === importance)

  return (
    <div className="ble-card">
      <div className="devices-card-title">
        <h3>Error handling</h3>
        {step && <button type="button" className="devices-card-clear" onClick={() => w.removeSetup(step.id)} title="Back to the firmware default"><Trash2 aria-hidden="true" />Clear</button>}
      </div>
      <p className="devices-muted">Choose which error severities this device handles. Low handles Critical only; Medium also handles High; High also handles Medium; Critical handles all. A Critical error always stops the system. Disabled (the default) ignores noncritical errors.</p>
      <div className="devices-error-grid">
        <label className="devices-error-action">
          <span>Importance</span>
          <select className={`devices-importance is-level-${importance}`} value={importance} onChange={(event) => save({ importance: Number(event.target.value) })}>
            {choices.map((choice) => <option key={choice.value} value={choice.value} title={choice.description}>{choice.label}{choice.value === 0 && !step ? ' (default)' : ''}</option>)}
          </select>
        </label>
        {current?.description && <p className="devices-hint">{current.description}</p>}
      </div>
      <h4 className="devices-subheading">When an error happens</h4>
      <ErrorActionsField value={actions} actions={w.actions} importance={importance} onChange={(next) => save({ actions: next })} />
    </div>
  )
}
