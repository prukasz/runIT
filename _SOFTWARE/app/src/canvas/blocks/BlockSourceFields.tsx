import { SelectField } from '../../components/FormField'
import { runitDeviceCatalog } from '../../domain/descriptors'
import type { DeviceCatalog, VmBlockField, VmBlockType } from '../../domain/descriptors'
import { allDevices, eventSourceChannels, eventSourceDevices, eventSourceEvents, pinDisplayLabel } from '../../domain/devices'
import type { CanvasBlock, ProjectDevice } from '../../domain/project'
import { enumSettingValue, sourceDependents, withoutSettings } from './blockSources'

const ANY = 'any'

/** One picker: "Any" (the wildcard, stored as no setting) or one of the choices. A stored choice the catalog no longer offers stays, marked. */
function SourceSelect({ label, type, field, block, choices, empty, onUpdate }: { label: string; type: VmBlockType; field: VmBlockField; block: CanvasBlock; choices: readonly { value: number; label: string }[]; empty?: string; onUpdate: (change: (block: CanvasBlock) => CanvasBlock) => void }) {
  const raw = block.settings?.[field.name]
  const value = typeof raw === 'number' && raw !== field.anyValue ? raw : undefined
  const set = (next: number | undefined) => onUpdate((current) => {
    const cleared = withoutSettings(current, [field.name, ...sourceDependents(type, field.name).map((entry) => entry.name)])
    return next === undefined ? cleared : { ...cleared, settings: { ...cleared.settings, [field.name]: next } }
  })
  return <label className="block-field"><span>{label}</span><SelectField value={value ?? ANY} disabled={!choices.length && value === undefined} onChange={(event) => set(event.target.value === ANY ? undefined : Number(event.target.value))}>
    <option value={ANY}>Any</option>
    {value !== undefined && !choices.some((choice) => choice.value === value) && <option value={value}>{`${value} (unavailable)`}</option>}
    {choices.map((choice) => <option key={choice.value} value={choice.value}>{choice.label}</option>)}
  </SelectField>{!choices.length && empty && <em>{empty}</em>}</label>
}

/** The pickers of a device chosen from an event catalog: the device, then its channel and event, narrowed by the domain setting. */
export function BlockSourceFields({ block, type, devices, deviceCatalog, onUpdate }: { block: CanvasBlock; type: VmBlockType; devices: readonly ProjectDevice[]; deviceCatalog?: DeviceCatalog; onUpdate: (change: (block: CanvasBlock) => CanvasBlock) => void }) {
  const catalog = deviceCatalog ?? runitDeviceCatalog()
  return <>{type.fields.filter((field) => field.idKind === 'device' && field.sourceField).map((deviceField) => {
    const domainField = type.fields.find((field) => field.name === deviceField.sourceField)
    if (!domainField?.enumRef) return null
    const domain = enumSettingValue(type, domainField, block.settings?.[domainField.name])
    const chosen = block.settings?.[deviceField.name]
    const deviceId = typeof chosen === 'number' && chosen !== deviceField.anyValue ? chosen : undefined
    const device = deviceId === undefined ? undefined : allDevices(catalog, devices).find((entry) => entry.deviceId === deviceId)
    const dependents = type.fields.filter((field) => (field.idKind === 'channel' || field.idKind === 'event') && field.deviceField === deviceField.name)
    const channels = eventSourceChannels(catalog, devices, domainField.enumRef, domain, deviceId)
    return <div key={deviceField.name} className="block-hardware">
      <SourceSelect label="Device" type={type} field={deviceField} block={block} onUpdate={onUpdate} empty="No device raises events in this domain."
        choices={eventSourceDevices(catalog, devices, domainField.enumRef, domain).map((entry) => ({ value: entry.deviceId, label: `${entry.name} (#${entry.deviceId})` }))} />
      {dependents.map((field) => field.idKind === 'channel'
        ? <SourceSelect key={field.name} label={channels.label ?? 'Channel'} type={type} field={field} block={block} onUpdate={onUpdate} empty={deviceId === undefined ? 'Choose a device to pick a channel.' : 'This device has no channels.'}
            choices={channels.choices.map((choice) => ({ value: choice.value, label: pinDisplayLabel(catalog, device?.ref, choice.value, choice.label) }))} />
        : <SourceSelect key={field.name} label="Event" type={type} field={field} block={block} onUpdate={onUpdate} empty="Choose a domain to pick an event."
            choices={eventSourceEvents(catalog, devices, domainField.enumRef!, domain, deviceId)} />)}
    </div>
  })}</>
}
