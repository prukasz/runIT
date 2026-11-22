import { useMemo, useState } from 'react'
import { Bluetooth, Cable, Plus, RefreshCw, Terminal, Trash2, Wifi } from 'lucide-react'
import type { FieldValues } from '@backend'
import { Badge, Button, Card, CardStack, ChoiceField, FieldError, FormGrid, FormRow, NumberField, PanelHeader, Switch, TextField } from '@ui/components'
import type { Choice } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import { FieldsForm } from '../devices/FieldsForm'
import type { PinContext } from '../devices/FieldsForm'
import { CMD_SEND_NOWAIT, PROV_TYPE } from './routing'
import type { RoutingState } from './useRouting'
import './Routing.css'

const hex4 = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(4, '0')}`
const parseHex16 = (text: string): number | undefined => {
  const t = text.trim()
  const n = Number(t.startsWith('0x') || t.startsWith('0X') ? t : `0x${t}`)
  return Number.isInteger(n) && n >= 0 && n <= 0xffff ? n : undefined
}

const providerIcon = (type: number) =>
  type === PROV_TYPE.ble ? Bluetooth
  : type === PROV_TYPE.console ? Terminal
  : type === PROV_TYPE.mqtt ? Wifi
  : Cable

export function ProvidersScreen({ routing, pinContext, uartDevices, onOpenMatrix }: {
  routing: RoutingState
  pinContext: PinContext
  uartDevices: readonly { readonly id: number; readonly name: string }[]
  onOpenMatrix: () => void
}) {
  const catalog = routing.services.catalog
  const providerTypes = useMemo(() => [...catalog.providers.values()], [catalog])
  const typeChoices: Choice<string>[] = useMemo(
    () => providerTypes.map((p) => ({ value: p.id, label: p.label, ...(p.description ? { description: p.description } : {}) })),
    [providerTypes],
  )

  const usedIds = useMemo(() => new Set(routing.snapshot.providers.map((p) => p.id)), [routing.snapshot.providers])
  const nextFreeId = useMemo(() => {
    let id = 8
    while (usedIds.has(id) && id < 127) id++
    return id
  }, [usedIds])

  const takenUuids = useMemo(() => new Set(routing.snapshot.gatt.flatMap((s) => [s.uuid, ...s.characteristics.map((c) => c.uuid)])), [routing.snapshot.gatt])
  const freeUuid = (start: number) => {
    let u = start
    while (takenUuids.has(u) && u < 0xffdf) u++
    return u
  }

  const [providerId, setProviderId] = useState<number>(nextFreeId)
  const [selectedType, setSelectedType] = useState<string>(providerTypes[0]?.id ?? 'ble')
  // BLE fields
  const [bleSvc, setBleSvc] = useState<number>(() => freeUuid(0xee00))
  const [bleTx, setBleTx] = useState<number>(() => freeUuid(0xee01))
  const [bleRx, setBleRx] = useState<number>(0)
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
  const sessionsByProvider = useMemo(() => new Map(routing.snapshot.sessions.map((s) => [s.provider, s])), [routing.snapshot.sessions])

  const handleCreate = async () => {
    setLocalError(undefined)
    if (providerId < 1 || providerId > 254) {
      setLocalError('Provider ID must be between 1 and 254 (use 1..127 if you want to attach a session).')
      return
    }
    if (usedIds.has(providerId)) {
      setLocalError(`Provider #${providerId} already exists on the board.`)
      return
    }
    let cfg: FieldValues
    if (selectedType === 'ble') {
      if (bleSvc === 0) {
        setLocalError('BLE service UUID must be non-zero (e.g. 0xEE00).')
        return
      }
      if (bleTx === 0 && bleRx === 0) {
        setLocalError('Give the BLE provider at least an Out (notify) or In (write) characteristic UUID.')
        return
      }
      cfg = {
        svc: bleSvc,
        tx: bleTx,
        rx: bleRx,
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
      let next = providerId + 1
      while (usedIds.has(next) && next < 127) next++
      setProviderId(next)
      if (selectedType === 'ble') {
        setBleTx((u) => (u > 0 ? u + 2 : 0))
        setBleRx((u) => (u > 0 ? u + 2 : 0))
      }
    }
  }

  const uartChoices: Choice<number>[] = useMemo(
    () => uartDevices.map((d) => ({ value: d.id, label: `#${d.id} ${d.name}` })),
    [uartDevices],
  )

  return (
    <div className="project-screen">
      <PanelHeader title="Providers">
        <span className="settings-actions">
          <Button size="sm" onClick={onOpenMatrix}>Open Data matrix</Button>
          <Button size="sm" disabled={!routing.connected || !!routing.busy} onClick={() => void routing.refresh()}>
            <RefreshCw aria-hidden="true" className={routing.busy ? 'spinning' : undefined} />
            Refresh
          </Button>
        </span>
      </PanelHeader>

      {!routing.connected && (
        <p className="project-note">Not connected: showing the board&apos;s default providers as a preview. Connect the board to inspect, create or remove providers live.</p>
      )}
      {routing.note && <p className="routing-note">{routing.note}</p>}
      <FieldError>{localError ?? routing.error}</FieldError>

      <CardStack>
        <ErrorsCard about={`router_key:${routing.link.defs.keys.provider}`} />

        <Card
          title={`Create provider`}
          help="Creates a new data provider on the board immediately (PROVIDER CREATE). Providers with ID 1..127 can host a session in the Data matrix."
          actions={
            <Button size="sm" variant="primary" disabled={!routing.connected || !!routing.busy} onClick={() => void handleCreate()}>
              <Plus aria-hidden="true" />Create
            </Button>
          }
        >
          <FormGrid>
            <FormRow label="Provider ID" note="Unique ID (1..127 recommended so a session can attach to it).">
              <NumberField aria-label="Provider ID" integer min={1} max={254} value={providerId} onCommit={setProviderId} />
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
                <FormRow label="Out UUID (notify)" note="0 or empty = no outbound characteristic.">
                  <TextField aria-label="Out UUID" key={`tx:${bleTx}`} defaultValue={bleTx ? hex4(bleTx) : ''} placeholder="0xEE01" onBlur={(e) => {
                    const t = e.target.value.trim()
                    if (!t) return setBleTx(0)
                    const n = parseHex16(t)
                    if (n !== undefined) setBleTx(n)
                  }} />
                </FormRow>
                <FormRow label="In UUID (write)" note="0 or empty = no inbound characteristic.">
                  <TextField aria-label="In UUID" key={`rx:${bleRx}`} defaultValue={bleRx ? hex4(bleRx) : ''} placeholder="0xEE02 (optional)" onBlur={(e) => {
                    const t = e.target.value.trim()
                    if (!t) return setBleRx(0)
                    const n = parseHex16(t)
                    if (n !== undefined) setBleRx(n)
                  }} />
                </FormRow>
                <FormRow label="Name" note="BLE characteristic user description (up to 15 chars).">
                  <TextField aria-label="BLE provider name" maxLength={15} value={bleName} onChange={(e) => setBleName(e.target.value)} />
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
                <FormRow label="UART device" note="A CONTRACT_UART device already created on the board.">
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
        </Card>

        <Card
          title={`Active providers (${routing.snapshot.providers.length})`}
          help="Every provider currently registered in sys_data_provider on the board. System providers belong to the board's boot setup and cannot be removed."
        >
          <div className="routing-provider-list">
            {routing.snapshot.providers.map((p) => {
              const Icon = providerIcon(p.type)
              const ses = sessionsByProvider.get(p.id)
              return (
                <div key={p.id} className="routing-provider-row">
                  <div className="routing-provider-icon"><Icon aria-hidden="true" /></div>
                  <div className="routing-provider-main">
                    <div className="routing-provider-head">
                      <span className="routing-provider-title">Provider #{p.id}</span>
                      <Badge tone="info" size="compact">{p.typeLabel}</Badge>
                      {p.system && <Badge tone="neutral" size="compact">system</Badge>}
                      {p.up ? <Badge tone="success" size="compact">up</Badge> : <Badge tone="warning" size="compact">down</Badge>}
                      {p.framed ? <Badge tone="info" size="compact">framed</Badge> : <Badge tone="neutral" size="compact">raw</Badge>}
                      {p.wired && <Badge tone="neutral" size="compact">wired</Badge>}
                      {ses && (
                        <Badge tone={ses.open ? 'success' : 'neutral'} size="compact">
                          {ses.open ? `session · peer 0x${(ses.peerId ?? 0).toString(16).toUpperCase()}` : 'session attached'}
                        </Badge>
                      )}
                    </div>
                  </div>
                  <div className="routing-provider-actions">
                    {!p.system && (
                      <Button size="sm" variant="danger" disabled={!routing.connected || !!routing.busy} title={`Remove provider #${p.id}`} onClick={() => void routing.removeProvider(p.id)}>
                        <Trash2 aria-hidden="true" />Remove
                      </Button>
                    )}
                  </div>
                </div>
              )
            })}
          </div>
        </Card>

        {routing.snapshot.gatt.length > 0 && (
          <Card title={`BLE GATT table (${routing.snapshot.gatt.length})`} help="Live GATT services and characteristics published by sys_ble (BLE LIST).">
            <div className="routing-gatt-list">
              {routing.snapshot.gatt.map((svc) => (
                <div key={svc.uuid} className="routing-gatt-svc">
                  <div className="routing-gatt-svc-head">
                    <code>{hex4(svc.uuid)}</code>
                    {svc.locked && <Badge tone="neutral" size="compact">locked</Badge>}
                    {svc.primary && <Badge tone="info" size="compact">primary</Badge>}
                  </div>
                  <div className="routing-gatt-chrs">
                    {svc.characteristics.map((c) => (
                      <span key={c.uuid} className="routing-gatt-chr">
                        <code>{hex4(c.uuid)}</code>
                        <span>{c.name || 'unnamed'}</span>
                        {c.notify && <Badge tone="info" size="compact">TX</Badge>}
                        {c.write && <Badge tone="warning" size="compact">RX</Badge>}
                        {c.framed && <Badge tone="success" size="compact">framed</Badge>}
                      </span>
                    ))}
                  </div>
                </div>
              ))}
            </div>
          </Card>
        )}
      </CardStack>
    </div>
  )
}
