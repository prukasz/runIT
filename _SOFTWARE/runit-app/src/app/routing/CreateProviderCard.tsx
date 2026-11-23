import { useMemo, useState } from 'react'
import { Plus } from 'lucide-react'
import type { FieldValues } from '@backend'
import { Button, Card, ChoiceField, FieldError, FormGrid, FormRow, NumberField, Switch, TextField } from '@ui/components'
import type { Choice } from '@ui/components'
import { FieldsForm } from '../devices/FieldsForm'
import type { PinContext } from '../devices/FieldsForm'
import { CMD_SEND_NOWAIT } from './routing'
import type { RoutingState } from './useRouting'
import type { ProviderRegistry } from './providerRegistry'
import './Routing.css'

const hex4 = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(4, '0')}`
const parseHex16 = (text: string): number | undefined => {
  const t = text.trim()
  const n = Number(t.startsWith('0x') || t.startsWith('0X') ? t : `0x${t}`)
  return Number.isInteger(n) && n >= 0 && n <= 0xffff ? n : undefined
}

/** The card that makes a provider on the board (PROVIDER CREATE): its ID, its name in the project, its type and the type's config. */
export function CreateProviderCard({ routing, registry, pinContext, uartDevices, onName }: {
  registry: ProviderRegistry
  routing: RoutingState
  /** Gives a provider its name in the project (empty: none). */
  onName: (id: number, name: string) => void
  pinContext: PinContext
  uartDevices: readonly { readonly id: number; readonly name: string }[]
}) {
  const catalog = routing.services.catalog
  const providerTypes = useMemo(() => [...catalog.providers.values()], [catalog])
  const typeChoices: Choice<string>[] = useMemo(
    () => providerTypes.map((p) => ({ value: p.id, label: p.label, ...(p.description ? { description: p.description } : {}) })),
    [providerTypes],
  )

  // The id is the register's lowest free one (1..127 can host a session); the user never sees it.
  const providerId = useMemo(() => registry.freeFrom(8, 127), [registry])

  const takenUuids = useMemo(() => new Set(routing.snapshot.gatt.flatMap((s) => [s.uuid, ...s.characteristics.map((c) => c.uuid)])), [routing.snapshot.gatt])
  const freeUuid = (start: number) => {
    let u = start
    while (takenUuids.has(u) && u < 0xffdf) u++
    return u
  }

  const [providerName, setProviderName] = useState<string>('')
  const [selectedType, setSelectedType] = useState<string>(providerTypes[0]?.id ?? 'ble')
  // BLE fields
  const [bleSvc, setBleSvc] = useState<number>(() => freeUuid(0xee00))
  const [bleChr, setBleChr] = useState<number>(() => freeUuid(0xee01))
  const [bleFramed, setBleFramed] = useState<boolean>(false)
  const [bleNoWait, setBleNoWait] = useState<boolean>(true)
  const [bleName, setBleName] = useState<string>('custom')
  // UART fields
  const [uartDevice, setUartDevice] = useState<number>(uartDevices[0]?.id ?? 1)
  const [uartNoWait, setUartNoWait] = useState<boolean>(true)
  // Generic fallback for any future provider descriptor
  const [genericCfg, setGenericCfg] = useState<FieldValues>({})
  const [localError, setLocalError] = useState<string | undefined>()

  const activeDescriptor = catalog.providers.get(selectedType)

  const handleCreate = async () => {
    setLocalError(undefined)
    if (providerId === undefined) {
      setLocalError('Every provider id is taken: remove a provider first.')
      return
    }
    let cfg: FieldValues
    if (selectedType === 'ble') {
      if (bleSvc === 0) {
        setLocalError('BLE service UUID must be non-zero (e.g. 0xEE00).')
        return
      }
      if (bleChr === 0) {
        setLocalError('BLE characteristic UUID must be non-zero (e.g. 0xEE01).')
        return
      }
      cfg = {
        svc: bleSvc,
        chr: bleChr,
        flags: bleFramed ? routing.link.defs.ble.providerFramed : 0,
        rx_flags: bleNoWait ? CMD_SEND_NOWAIT : 0,
        name: bleName.trim(),
      }
    } else if (selectedType === 'uart') {
      cfg = {
        device: uartDevice,
        flags: uartNoWait ? CMD_SEND_NOWAIT : 0,
      }
    } else {
      cfg = genericCfg
    }
    const ok = await routing.createProvider(providerId, selectedType, cfg)
    if (ok) {
      if (providerName.trim()) onName(providerId, providerName.trim())
      setProviderName('')
      if (selectedType === 'ble') {
        setBleChr((u) => (u > 0 ? u + 2 : 0))
      }
    }
  }

  const uartChoices: Choice<number>[] = useMemo(
    () => uartDevices.map((d) => ({ value: d.id, label: d.name })),
    [uartDevices],
  )

  return (
    <Card
      title="New provider"
      help="Creates a new data provider on the board immediately (PROVIDER CREATE). Providers with ID 1..127 can host a session."
      actions={
        <Button size="sm" variant="primary" disabled={!routing.connected || !!routing.busy} onClick={() => void handleCreate()}>
          <Plus aria-hidden="true" />Create
        </Button>
      }
    >
      <FormGrid>
        <FormRow label="Name" note="Your name for it, kept in the project: the board knows a provider by its ID and type only.">
          <TextField aria-label="Provider name" maxLength={32} value={providerName} placeholder={providerId === undefined ? '' : registry.name(providerId)} onChange={(e) => setProviderName(e.target.value)} />
        </FormRow>
        <FormRow label="Type" note={activeDescriptor?.description ?? 'Driver type from generated/providers.'}>
          <ChoiceField<string> aria-label="Provider type" value={selectedType} choices={typeChoices} onChange={(v) => v && setSelectedType(v)} />
        </FormRow>
      </FormGrid>

      {selectedType === 'ble' ? (
        <div className="routing-cfg-block">
          <FormGrid>
            <FormRow label="Service UUID" note="16-bit GATT service UUID (created if new, joined if unlocked).">
              <TextField aria-label="Service UUID" key={`svc:${bleSvc}`} defaultValue={hex4(bleSvc)} onBlur={(e) => {
                const n = parseHex16(e.target.value)
                if (n !== undefined && n > 0) setBleSvc(n)
              }} />
            </FormRow>
            <FormRow label="Characteristic UUID" note="The one characteristic: the client writes it, the board notifies on it. What goes which way is the provider's links in the data matrix.">
              <TextField aria-label="Characteristic UUID" key={`chr:${bleChr}`} defaultValue={hex4(bleChr)} placeholder="0xEE01" onBlur={(e) => {
                const n = parseHex16(e.target.value.trim())
                if (n !== undefined && n > 0) setBleChr(n)
              }} />
            </FormRow>
            <FormRow label="Description" note="The characteristic's user description a BLE client sees (up to 15 chars).">
              <TextField aria-label="BLE characteristic description" maxLength={15} value={bleName} onChange={(e) => setBleName(e.target.value)} />
            </FormRow>
            <FormRow label="Framed" note="On: carries whole runIT frames (required for a session). Off: raw stream bytes.">
              <Switch label={bleFramed ? 'Framed (session-ready)' : 'Raw stream'} checked={bleFramed} onChange={setBleFramed} />
            </FormRow>
            <FormRow label="RX non-blocking" note="Set CMD_SEND_NOWAIT so the BLE host never stalls on slow consumers.">
              <Switch label={bleNoWait ? 'NOWAIT' : 'Blocking'} checked={bleNoWait} onChange={setBleNoWait} />
            </FormRow>
          </FormGrid>
        </div>
      ) : selectedType === 'uart' ? (
        <div className="routing-cfg-block">
          <FormGrid>
            <FormRow label="UART device" note="A CONTRACT_UART device already created on the board, in a raw mode: a framed one has its provider already, made with it.">
              {uartChoices.length > 0 ? (
                <ChoiceField<number> aria-label="UART device" value={uartDevice} choices={uartChoices} onChange={(v) => v !== undefined && setUartDevice(v)} />
              ) : (
                <NumberField aria-label="UART device ID" integer min={0} max={63} value={uartDevice} onCommit={setUartDevice} />
              )}
            </FormRow>
            <FormRow label="RX non-blocking" note="Set CMD_SEND_NOWAIT so the UART RX task drops instead of waiting.">
              <Switch label={uartNoWait ? 'NOWAIT' : 'Wait'} checked={uartNoWait} onChange={setUartNoWait} />
            </FormRow>
          </FormGrid>
        </div>
      ) : activeDescriptor?.config ? (
        <div className="routing-cfg-block">
          <FieldsForm layout={activeDescriptor.config} values={genericCfg} context={pinContext} onChange={setGenericCfg} />
        </div>
      ) : null}
      <FieldError>{localError ?? routing.error}</FieldError>
    </Card>
  )
}
