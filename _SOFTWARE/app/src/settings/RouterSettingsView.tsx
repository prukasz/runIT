import { useCallback, useMemo, useState } from 'react'
import type { ReactNode } from 'react'
import { Card, CardStack } from '../components/Card'
import { ChoiceField } from '../components/ChoiceField'
import { FormRow, NumberField } from '../components/FormField'
import { Switch } from '../components/Switch/Switch'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { configFromJson, curveFor, defaultConfig, settingsOps } from '../domain/settings'
import type { ConfigField, ConfigOp, ConfigValues } from '../domain/settings'
import { CurveChart } from './CurveChart'
import './Settings.css'

/*
 * A router's settings page: one form per op the firmware tags `@settings` (domain/settings/contractConfig.ts), each built from the
 * op's argument struct in the generated contracts: aliases, descriptions, choices, limits, units and `@default`. Nothing about a
 * setting is written in the app, so a field added in C, or a new `@settings` op, shows up after the descriptors are regenerated.
 * The values are auto-saved to browser storage per router. A curve the firmware publishes for a choice (the LiPo charge curve of a
 * battery chemistry) is drawn under the form while that choice is picked.
 */

/** The values of every settings op of a router (`ROUTER_KEY_POWER`), by op symbol. */
export function useRouterSettings(routerSymbol: string) {
  const ops = useMemo(() => settingsOps(routerSymbol), [routerSymbol])
  const storageKey = `runit.settings.cfg.${routerSymbol}`
  const [values, setValues] = useState<Readonly<Record<string, ConfigValues>>>(() =>
    readStored(storageKey, (raw) => {
      const saved: unknown = JSON.parse(raw)
      if (typeof saved !== 'object' || saved === null || Array.isArray(saved)) return undefined
      return Object.fromEntries(ops.map((op) => [op.symbol, configFromJson(op, JSON.stringify((saved as Record<string, unknown>)[op.symbol] ?? {})) ?? defaultConfig(op)]))
    }, Object.fromEntries(ops.map((op) => [op.symbol, defaultConfig(op)]))),
  )
  usePersistEffect(storageKey, values)
  const update = useCallback((opSymbol: string, name: string, value: number) => setValues((current) => ({ ...current, [opSymbol]: { ...current[opSymbol], [name]: value } })), [])
  const reset = useCallback((op: ConfigOp) => setValues((current) => ({ ...current, [op.symbol]: defaultConfig(op) })), [])
  /** Puts the values read from the board in the forms; an op the board sent nothing for goes back to its default. */
  const replace = useCallback((found: Readonly<Record<string, ConfigValues>>) => setValues(Object.fromEntries(ops.map((op) => [op.symbol, found[op.symbol] ? { ...defaultConfig(op), ...found[op.symbol] } : defaultConfig(op)]))), [ops])
  return { ops, values, update, reset, replace }
}

export type RouterSettingsState = ReturnType<typeof useRouterSettings>

/** A field with exactly the choices No / Yes is an on / off switch. */
const isSwitch = (field: ConfigField): boolean => field.choices?.length === 2 && field.choices[0]?.value === 0 && field.choices[1]?.value === 1 && field.choices[0].alias.toLowerCase() === 'no' && field.choices[1].alias.toLowerCase() === 'yes'

function FieldRow({ field, value, onChange }: { field: ConfigField; value: number; onChange: (value: number) => void }) {
  if (isSwitch(field)) {
    return (
      <div>
        <Switch label={field.alias} checked={value !== 0} onChange={(checked) => onChange(checked ? 1 : 0)} />
        {field.description && <p className="program-muted" style={{ margin: '0 0 8px 62px' }}>{field.description}</p>}
      </div>
    )
  }
  const label = field.unit ? `${field.alias} (${field.unit})` : field.alias
  return (
    <FormRow label={label} {...(field.description ? { hint: field.description } : {})}>
      {field.choices ? (
        <ChoiceField<number> selectClassName="form-field-panel" value={value} choices={field.choices.map((choice) => ({ value: choice.value, label: choice.alias }))} onChange={(next) => next !== undefined && onChange(next)} />
      ) : (
        <NumberField integer className="form-field-panel" value={value} {...(field.min !== undefined ? { min: field.min } : {})} {...(field.max !== undefined ? { max: field.max } : {})} onCommit={onChange} />
      )}
    </FormRow>
  )
}

/** One op as a card: its fields as the descriptor lists them, then the curve of a chosen choice. */
function ConfigCard({ workspace, op }: { workspace: RouterSettingsState; op: ConfigOp }) {
  const values = workspace.values[op.symbol] ?? defaultConfig(op)
  const defaults = useMemo(() => defaultConfig(op), [op])
  const changed = op.fields.some((field) => values[field.name] !== defaults[field.name])
  return (
    <Card title={op.alias} subhead={op.description}>
      {op.fields.filter((field) => !field.hidden).map((field) => <FieldRow key={field.name} field={field} value={values[field.name] ?? 0} onChange={(value) => workspace.update(op.symbol, field.name, value)} />)}
      {op.skipped.length > 0 && <p className="program-muted">Not edited here, left to the board: {op.skipped.join(', ')}.</p>}
      <ChoiceCurve op={op} values={values} />
      {changed && <button type="button" className="conn-add-btn" title="Put every field back to its start value" onClick={() => workspace.reset(op)}>Restore defaults</button>}
    </Card>
  )
}

/** The curve of the chosen choice (a battery chemistry), with the op's fields that share its y unit drawn as levels. */
function ChoiceCurve({ op, values }: { op: ConfigOp; values: ConfigValues }) {
  for (const field of op.fields) {
    const choice = field.choices?.find((entry) => entry.value === values[field.name])
    const curve = curveFor(choice?.symbol)
    if (!curve) continue
    const levels = op.fields.filter((other) => other.unit === curve.y.unit).map((other) => ({ label: other.alias, value: values[other.name] ?? 0 }))
    return <CurveChart curve={curve} levels={levels} />
  }
  return null
}

/** The page of one router: every `@settings` op as a card. */
export function RouterSettingsEditor({ workspace, title, icon }: { workspace: RouterSettingsState; title: string; icon: ReactNode }) {
  return (
    <div className="object-editor">
      <div className="ble-general-header">
        {icon}
        <h1>{title}</h1>
      </div>
      <CardStack>
        {workspace.ops.map((op) => <ConfigCard key={op.symbol} workspace={workspace} op={op} />)}
      </CardStack>
    </div>
  )
}
