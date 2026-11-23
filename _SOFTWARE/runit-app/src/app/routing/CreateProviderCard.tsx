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
  // A Bluetooth way is made in the Bluetooth settings, not here.
  const providerTypes = useMemo(() => [...catalog.providers.values()].filter((p) => p.id !== 'ble'), [catalog])
  const typeChoices: Choice<string>[] = useMemo(
    () => providerTypes.map((p) => ({ value: p.id, label: p.label, ...(p.description ? { description: p.description } : {}) })),
    [providerTypes],
  )

  // The id is the register's lowest free one (1..127 can host a session); the user never sees it.
  const providerId = useMemo(() => registry.freeFrom(8, 127), [registry])

  const [providerName, setProviderName] = useState<string>('')
  const [selectedType, setSelectedType] = useState<string>(providerTypes[0]?.id ?? 'uart')
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
    if (selectedType === 'uart') {
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

      {selectedType === 'uart' ? (
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
