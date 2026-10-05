import { ChoiceField } from '../../components/ChoiceField'
import { runitDeviceCatalog } from '../../domain/descriptors'
import type { DeviceCatalog, VmBlockType } from '../../domain/descriptors'
import type { CanvasBlock, ProjectAction } from '../../domain/project'
import { enumSettingValue } from './blockSources'

/** The action picker of a block (`@id action @source-field scope`): the built-in actions or the recorded ones, as its scope says. */
export function BlockActionFields({ block, type, actions, deviceCatalog, wiredTo, onUpdate }: { block: CanvasBlock; type: VmBlockType; actions: readonly ProjectAction[]; deviceCatalog?: DeviceCatalog; wiredTo?: string; onUpdate: (change: (block: CanvasBlock) => CanvasBlock) => void }) {
  const catalog = deviceCatalog ?? runitDeviceCatalog()
  return <>{type.fields.filter((field) => field.idKind === 'action').map((field) => {
    const scopeField = type.fields.find((entry) => entry.name === field.sourceField)
    // Scope 0 is the built-in actions; anything else is a recorded one.
    const recorded = !!scopeField && (enumSettingValue(type, scopeField, block.settings?.[scopeField.name]) ?? 0) !== 0
    const choices = recorded
      ? actions.map((action) => ({ value: action.actionId, label: `${action.name} (ID ${action.actionId})`, description: undefined as string | undefined }))
      : catalog.staticActions.map((action) => ({ value: action.value, label: action.label, description: action.description }))
    const raw = block.settings?.[field.name]
    const value = typeof raw === 'number' ? raw : undefined
    return <label key={field.name} className="block-field">
      <span>{type.inputs.pins.find((pin) => pin.index === field.overriddenBy)?.title ?? 'Action'}</span>
      <ChoiceField value={value} placeholder="Select an action" choices={choices} unavailableLabel={(id) => `Action ${id} (unavailable)`} onChange={(next) => next !== undefined && onUpdate((current) => ({ ...current, settings: { ...current.settings, [field.name]: next } }))} />
      <em>{wiredTo ? `Not used while the input is wired to ${wiredTo}` : recorded && !choices.length ? 'No recorded action yet: record one in the Board view' : 'Used when no input is connected'}</em>
    </label>
  })}</>
}
