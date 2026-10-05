import type { DeviceCatalog } from '../../domain/descriptors'
import { ChoiceField } from '../../components/ChoiceField'
import { TextField } from '../../components/FormField'
import { runitDeviceCatalog } from '../../domain/descriptors'
import type { VmBlockType } from '../../domain/descriptors'
import type { CanvasBlock, ProjectDevice } from '../../domain/project'
import { isDynamicInput, pinMaskText, readPinMask } from '../../domain/project/blockPins'
import { BlockSwitch } from './BlockSwitch'
import { blockDevicePins, blockDevices } from './blockDevicePins'
import { pinDisplayLabel } from '../../domain/devices'
import { ArrowUpRight } from 'lucide-react'

export function BlockHardwareFields({ block, type, devices, deviceCatalog, onUpdate, onOpenDevice, onOpenPin }: { block: CanvasBlock; type: VmBlockType; devices: readonly ProjectDevice[]; deviceCatalog?: DeviceCatalog; onUpdate: (change: (block: CanvasBlock) => CanvasBlock) => void; onOpenDevice?: (ref: string) => void; onOpenPin?: (ref: string, pin: number) => void }) {
  const catalog = deviceCatalog ?? runitDeviceCatalog()
  return <>{type.fields.filter((field) => field.idKind === 'device' && !field.sourceField).map((deviceField) => {
    // No device until one is chosen: the selector starts empty instead of standing for device 0.
    const chosen = block.settings?.[deviceField.name]
    const deviceId = chosen === undefined ? undefined : Number(chosen)
    const targets = blockDevices(catalog, devices, deviceField)
    const targetRef = deviceId === undefined ? undefined : targets.find((device) => device.deviceId === deviceId)?.ref
    const pinFields = type.fields.filter((field) => field.idKind === 'pin' && field.deviceField === deviceField.name)
    const pins = deviceId === undefined ? [] : blockDevicePins(catalog, devices, deviceId, deviceField.contract)
    const set = (name: string, value: number | string) => onUpdate((current) => ({ ...current, settings: { ...current.settings, [name]: value } }))
    return <div key={deviceField.name} className="block-hardware">
      <div className="block-hardware-link-row"><label className="block-field"><span>Device</span><ChoiceField value={deviceId} placeholder="Select a device" unavailableLabel={(id) => `Device ${id} (unavailable)`}
        choices={targets.map((device) => ({ value: device.deviceId, label: `${device.name} (#${device.deviceId})` }))}
        onChange={(next) => {
          if (next === undefined) return
          const firstPin = blockDevicePins(catalog, devices, next, deviceField.contract)[0]?.value
          onUpdate((current) => {
            const settings = { ...current.settings, [deviceField.name]: next }
            for (const field of pinFields) {
              if (firstPin === undefined) delete settings[field.name]
              else settings[field.name] = firstPin
              for (const mask of type.fields.filter((mask) => mask.letUserSelectAvailable === field.name)) settings[mask.name] = pinMaskText(firstPin === undefined ? 0n : 1n << BigInt(firstPin))
            }
            return { ...current, settings }
          })
        }} /></label><button type="button" className="block-hardware-jump" disabled={!targetRef} onClick={() => targetRef && onOpenDevice?.(targetRef)} title="Go to device" aria-label="Go to selected device"><ArrowUpRight aria-hidden="true" /></button></div>
      {deviceId !== undefined && pinFields.map((pinField) => {
        const pin = Number(block.settings?.[pinField.name] ?? 0)
        const maskField = type.fields.find((field) => field.letUserSelectAvailable === pinField.name)
        const dynamic = maskField?.dynamicInput !== undefined && isDynamicInput(block, maskField.dynamicInput)
        const mask = readPinMask(maskField ? block.settings?.[maskField.name] : undefined) ?? 0n
        const available = maskField ? pins.filter((pin) => pin.value < maskField.size * 8) : pins
        return <div key={pinField.name} className="block-hardware-pin">
          <div className="block-hardware-link-row"><label className="block-field"><span>{dynamic ? 'Default pin' : 'Pin'}</span><ChoiceField value={pin} unavailableLabel={(value) => `Pin ${value} (unavailable)`}
            choices={available.map((entry) => ({ value: entry.value, label: pinDisplayLabel(catalog, targetRef, entry.value, entry.label) }))}
            onChange={(next) => {
              if (next === undefined) return
              onUpdate((current) => ({ ...current, settings: { ...current.settings, [pinField.name]: next, ...(maskField ? { [maskField.name]: pinMaskText(dynamic ? mask | (1n << BigInt(next)) : 1n << BigInt(next)) } : {}) } }))
            }} /></label><button type="button" className="block-hardware-jump" disabled={!targetRef || !pins.some((entry) => entry.value === pin)} onClick={() => targetRef && onOpenPin?.(targetRef, pin)} title="Go to pin" aria-label={`Go to pin ${pin}`}><ArrowUpRight aria-hidden="true" /></button></div>
          {maskField && <>
            <BlockSwitch label="Pin selection" value={dynamic ? 'dynamic' : 'static'} options={[["static", "Static"], ["dynamic", "Dynamic"]]} onChange={(value) => onUpdate((current) => {
              const index = maskField.dynamicInput!
              const selected = (current.dynamicInputs ?? []).filter((entry) => entry !== index)
              const { dynamicInputs: _, ...rest } = current
              const inputs = current.inputs?.map((path, at) => at === index && value === 'static' ? null : path)
              return { ...rest, ...(inputs ? { inputs } : {}), ...(value === 'dynamic' || selected.length ? { dynamicInputs: value === 'dynamic' ? [...selected, index] : selected } : {}), settings: { ...current.settings, [maskField.name]: pinMaskText(mask | (1n << BigInt(pin))) } }
            })} />
            {dynamic && <fieldset className="block-pin-mask"><legend>Allowed pins</legend><p className="block-muted">Mark the pins the dynamic Pin input may select. The default pin stays included.</p><div>{available.map((entry) => <label key={entry.value}><TextField type="checkbox" checked={!!(mask & (1n << BigInt(entry.value)))} disabled={entry.value === pin} onChange={(event) => set(maskField.name, pinMaskText(event.target.checked ? mask | (1n << BigInt(entry.value)) : mask & ~(1n << BigInt(entry.value))))} /><span>{pinDisplayLabel(catalog, targetRef, entry.value, entry.label)}</span></label>)}</div></fieldset>}
          </>}
          {!available.length && <p className="block-muted">No available pins on this device.</p>}
        </div>
      })}
    </div>
  })}</>
}
