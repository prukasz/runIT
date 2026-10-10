import { useId } from 'react'
import { ChoiceField } from '../components/ChoiceField'
import { TextField } from '../components/FormField'
import { FieldLabel } from '../components/FieldNote'
import type { DeviceContract } from '../domain/descriptors'
import { initialParameterValue, parameterValue, SET_ERROR_HANDLING } from '../domain/devices'
import type { ProjectAction, StepValues } from '../domain/project'
import { ErrorActionsField } from './ErrorHandling'

export const initialValues = (contract: DeviceContract): Record<string, number | number[]> =>
  Object.fromEntries(contract.parameters.filter((parameter) => !parameter.instance).map((parameter) => [parameter.name, initialParameterValue(parameter)]))


/**
 * A contract's parameters as a form. The device ID (`instance`) isn't shown:
 * it comes from the device the contract runs on.
 */
export function ContractFields({ contract, values, onChange, actions = [], pinLabel }: {
  contract: DeviceContract
  values: StepValues
  onChange: (values: Record<string, number | readonly number[]>) => void
  /** The project's actions, for fields that pick one (error handling). */
  actions?: readonly ProjectAction[]
  pinLabel?: (pin: number, label: string) => string
}) {
  const set = (name: string, value: number | readonly number[]) => onChange({ ...values, [name]: value })
  const idBase = useId()
  return (
    <div className="contract-fields">
      {contract.parameters.some((parameter) => parameter.deviceWide) && <p className="devices-hint">Applies to the whole device.</p>}
      {contract.parameters.filter((parameter) => !parameter.deviceWide && !parameter.instance).map((parameter) => {
        const current = parameterValue(parameter, values)
        const id = `${idBase}-${parameter.name}`
        const label = `${parameter.label}${parameter.unit ? ` (${parameter.unit})` : ''}`
        if (Array.isArray(current) && contract.id === SET_ERROR_HANDLING && parameter.name === 'actions') {
          return (
            <fieldset key={parameter.name} className="contract-field is-array">
              <legend>{parameter.label}</legend>
              <ErrorActionsField value={current} actions={actions} importance={typeof values.importance === 'number' ? values.importance : contract.parameters.find((entry) => entry.name === 'importance')?.defaultValue ?? 0} onChange={(next) => set(parameter.name, next)} />
            </fieldset>
          )
        }
        if (Array.isArray(current)) {
          return (
            <fieldset key={parameter.name} className="contract-field is-array">
              <legend><FieldLabel note={parameter.note}>{label}</FieldLabel></legend>
              <div className="contract-array">
                {current.map((item, index) => (
                  <label key={index}>
                    <span>{index + 1}</span>
                    <TextField type="number" value={item} min={parameter.min} max={parameter.max} onChange={(event) => set(parameter.name, current.map((entry, at) => (at === index ? Number(event.target.value) : entry)))} />
                  </label>
                ))}
              </div>
            </fieldset>
          )
        }
        const value = current
        return (
          <div key={parameter.name} className="contract-field">
            <FieldLabel htmlFor={id} note={parameter.note}>{label}</FieldLabel>
            {parameter.boolean ? (
              <label className="contract-switch">
                <TextField id={id} type="checkbox" checked={value !== 0} onChange={(event) => set(parameter.name, event.target.checked ? 1 : 0)} />
                <span>{value ? 'On / high' : 'Off / low'}</span>
              </label>
            ) : parameter.choices ? (
              <ChoiceField id={id} value={value} onChange={(next) => next !== undefined && set(parameter.name, next)}
                choices={parameter.choices.map((choice) => ({ value: choice.value, label: parameter.name === 'pin' && pinLabel ? pinLabel(choice.value, choice.label) : choice.label === String(choice.value) ? choice.label : `${choice.label} (${choice.value})`, description: choice.description }))} />
            ) : (
              <TextField
                id={id}
                type="number"
                value={value}
                min={parameter.min}
                max={parameter.max}
                step={parameter.field.type === 'float' || parameter.field.type === 'double' ? 'any' : 1}
                onChange={(event) => set(parameter.name, Number(event.target.value))}
              />
            )}
          </div>
        )
      })}
    </div>
  )
}
