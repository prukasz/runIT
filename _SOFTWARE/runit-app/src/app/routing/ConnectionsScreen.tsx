import { useMemo, useState } from 'react'
import { Bluetooth, Cable, Link2, RefreshCw, Terminal, Trash2, Wifi } from 'lucide-react'
import type { SessionPeer } from '@backend'
import { Badge, Button, Card, CardStack, EditableField, FieldError, SegmentedSwitch, Switch, ToggleChip } from '@ui/components'
import { ErrorsCard } from '../console/ErrorView'
import type { PinContext } from '../devices/FieldsForm'
import { CreateProviderCard } from './CreateProviderCard'
import type { ProviderRegistry } from './providerRegistry'
import { chipLinks, chipState, deadmanChoices, deadmanLabel, sessionModules } from './connections'
import type { Endpoint } from './connections'
import { PROV_TYPE, providerPort, SESSION_ROLE, standardModulePorts } from './routing'
import type { LiveProvider, MatrixLink, ModulePortSpec } from './routing'
import type { RoutingState } from './useRouting'
import './Routing.css'

/*
 * The Connections window: the board's data matrix, its providers and their sessions in one place, made for a touch screen. A
 * provider is a card; what it carries is a row of chips, one per module of the board (tap to link, tap again to unlink: the links
 * a chip stands for are `connections.ts`); a provider that can host a session shows it: its peer, its dead-man (a row of
 * choices) and, for a session the app does not run on, its safety flag. A session whose peer is a board (a slave below this
 * board, or its master above) links the board's errors, events, entries and commands by itself. Providers are made at the foot.
 */

const providerIcon = (type: number) =>
  type === PROV_TYPE.ble ? Bluetooth : type === PROV_TYPE.console ? Terminal : type === PROV_TYPE.mqtt ? Wifi : Cable

const hex2 = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(2, '0')}`

/** Who is on the other end of a session: a client (the app, with the modules it is given), a slave board or the master board. */
const ROLE_OPTIONS = [['0', 'A client'], [String(SESSION_ROLE.slave), 'A slave board'], [String(SESSION_ROLE.master), 'The master board']] as const

const roleLabel = (role: number): string => (role === SESSION_ROLE.slave ? 'slave board' : role === SESSION_ROLE.master ? 'master board' : '')

const deadmanOptions = (ms: number) => deadmanChoices(ms).map((choice) => [String(choice), deadmanLabel(choice)] as const)

/** A provider's session: made from a dead-man, a safety flag and its modules; once made, its dead-man is changed here. */
function SessionBlock({ provider, session, routing, modules, known, own }: {
  provider: LiveProvider
  session?: SessionPeer
  routing: RoutingState
  modules: readonly ModulePortSpec[]
  known: readonly number[]
  own: boolean
}) {
  const [open, setOpen] = useState(false)
  const [deadman, setDeadman] = useState<number>(session?.deadmanMs ?? 3000)
  const [safety, setSafety] = useState<boolean>(session?.safety ?? true)
  const [role, setRole] = useState<number>(0)
  const [picked, setPicked] = useState<readonly number[]>(known.length ? known : [routing.link.defs.keys.router!])
  const [error, setError] = useState<string>()
  const busy = !routing.connected || !!routing.busy
  const changed = session !== undefined && (deadman !== session.deadmanMs || safety !== session.safety)

  if (!provider.framed || provider.id >= 128) return null

  const start = async () => {
    if (await routing.attachSession(provider.id, deadman, safety, picked, role)) setOpen(false)
  }
  const ownDeadman = async (ms: number) => {
    setError(undefined)
    try {
      await routing.link.setDeadman(ms)
      await routing.refresh()
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure))
    }
  }

  if (!session) {
    return (
      <div className="connections-session">
        <div className="connections-label">Session</div>
        {!open
          ? <Button disabled={busy} onClick={() => setOpen(true)}><Link2 aria-hidden="true" />Start a session</Button>
          : (
            <>
              <div className="connections-label">Who is on the other end</div>
              <SegmentedSwitch label="Who is on the other end" value={String(role)} options={ROLE_OPTIONS} onChange={(v) => setRole(Number(v))} />
              {role === 0
                ? (
                  <>
                    <div className="connections-label">Modules in it</div>
                    <div className="connections-chips">
                      {modules.map((m) => (
                        <ToggleChip key={m.moduleId} selected={picked.includes(m.moduleId)}
                          onClick={() => setPicked(picked.includes(m.moduleId) ? picked.filter((x) => x !== m.moduleId) : [...picked, m.moduleId])}>{m.name}</ToggleChip>
                      ))}
                    </div>
                  </>
                )
                : <p className="connections-status">Its errors, events, entries and commands are linked for you, the way a board's are.</p>}
              <div className="connections-label">Dead-man</div>
              <SegmentedSwitch label="Dead-man" value={String(deadman)} options={deadmanOptions(deadman)} onChange={(v) => setDeadman(Number(v))} />
              <Switch label="Safe state when it ends" checked={safety} onChange={setSafety} />
              <div className="project-actions">
                <Button variant="primary" disabled={busy || (role === 0 && picked.length === 0)} onClick={() => void start()}>Start</Button>
                <Button onClick={() => setOpen(false)}>Cancel</Button>
              </div>
            </>
          )}
      </div>
    )
  }

  const shown = own ? session.deadmanMs : deadman
  return (
    <div className="connections-session">
      <div className="connections-label">Session</div>
      <p className="connections-status">
        {session.open ? `Open · ${own ? 'this app' : `${roleLabel(session.role) || 'peer'} ${hex2(session.peerId ?? 0)}`}` : `Waiting for the ${roleLabel(session.role) || 'peer'}`}
        {session.safety ? ' · safe state when it ends' : ''}
      </p>
      <div className="connections-label">Dead-man</div>
      <SegmentedSwitch label="Dead-man" value={String(shown)} options={deadmanOptions(shown)} onChange={(v) => (own ? void ownDeadman(Number(v)) : setDeadman(Number(v)))} />
      {own
        ? <p className="connections-status">The board goes to its safe state when this app's link is silent for that long.</p>
        : (
          <>
            <Switch label="Safe state when it ends" checked={safety} onChange={setSafety} />
            <div className="project-actions">
              {changed && <Button variant="primary" disabled={busy} onClick={() => void routing.restartSession(provider.id, deadman, safety, known, session.role)}>Apply</Button>}
              <Button variant="danger" disabled={busy} onClick={() => void routing.detachSession(provider.id)}>End the session</Button>
            </div>
          </>
        )}
      <FieldError>{error}</FieldError>
    </div>
  )
}

function ProviderCard({ registry, provider, session, others, links, modules, routing, own, onName }: {
  registry: ProviderRegistry
  provider: LiveProvider
  session?: SessionPeer
  others: readonly LiveProvider[]
  links: readonly MatrixLink[]
  modules: readonly ModulePortSpec[]
  routing: RoutingState
  own: boolean
  onName: (id: number, name: string) => void
}) {
  const [error, setError] = useState<string>()
  const Icon = providerIcon(provider.type)
  const busy = !routing.connected || !!routing.busy
  const endpoint: Endpoint = session ? { kind: 'session', provider: provider.id } : { kind: 'provider', provider: provider.id }
  const known = useMemo(() => sessionModules(links, provider.id), [links, provider.id])

  /** Takes the links of a chip away when it is on, else makes the ones it lacks. */
  const toggle = async (expected: readonly MatrixLink[], on: boolean) => {
    for (const l of expected) {
      const there = links.some((x) => x.from === l.from && x.to === l.to)
      if (on && there && !(await routing.disconnectPorts(l.from, l.to))) return
      if (!on && !there && !(await routing.connectPorts(l.from, l.to))) return
    }
  }
  const toggleModule = async (spec: ModulePortSpec) => {
    setError(undefined)
    const expected = chipLinks(spec, endpoint)
    const on = chipState(links, expected) === 'on'
    if (!on && session && !known.includes(spec.moduleId)) {
      if (own) return setError("The app's own session has the modules the board gave it.")
      if (session.role) return setError("A board's session has the board's modules.")
      if (!(await routing.restartSession(provider.id, session.deadmanMs, session.safety, [...known, spec.moduleId]))) return
    }
    await toggle(expected, on)
  }

  return (
    <Card
      title={registry.name(provider.id)}
      actions={!provider.system
        ? <Button size="sm" variant="danger" disabled={busy} title={`Remove ${registry.name(provider.id)}`} onClick={() => void routing.removeProvider(provider.id)}><Trash2 aria-hidden="true" />Remove</Button>
        : undefined}
    >
      <div className="connections-head">
        <Icon aria-hidden="true" />
        <EditableField className="connections-name" value={provider.name ?? ''} maxLength={32} placeholder={registry.name(provider.id)} aria-label={`Name of ${registry.name(provider.id)}`} onChange={(name) => onName(provider.id, name)} />
        <Badge tone="info" size="compact">{provider.typeLabel}</Badge>
        {provider.system && <Badge tone="neutral" size="compact">system</Badge>}
        <Badge tone={provider.up ? 'success' : 'warning'} size="compact">{provider.up ? 'up' : 'down'}</Badge>
        <Badge tone="neutral" size="compact">{provider.framed ? 'framed' : 'raw'}</Badge>
      </div>

      <div className="connections-label">{session ? 'Carries, through its session' : 'Carries'}</div>
      <div className="connections-chips">
        {modules.map((m) => {
          const state = chipState(links, chipLinks(m, endpoint))
          return (
            <ToggleChip key={m.moduleId} selected={state === 'on'} {...(state === 'partial' ? { className: 'is-partial' } : {})} disabled={busy} title={m.description} onClick={() => void toggleModule(m)}>{m.name}</ToggleChip>
          )
        })}
      </div>

      {!session && others.length > 0 && (
        <>
          <div className="connections-label">Passes on to</div>
          <div className="connections-chips">
            {others.map((o) => {
              const expected = [{ from: providerPort(provider.id), to: providerPort(o.id) }]
              const on = chipState(links, expected) === 'on'
              return <ToggleChip key={o.id} selected={on} disabled={busy} onClick={() => void toggle(expected, on)}>{registry.name(o.id)}</ToggleChip>
            })}
          </div>
        </>
      )}

      <SessionBlock key={`${provider.id}:${session ? 'on' : 'off'}`} provider={provider} {...(session ? { session } : {})} routing={routing} modules={modules} known={known} own={own} />
      <FieldError>{error}</FieldError>
    </Card>
  )
}

export function ConnectionsScreen({ routing, registry, pinContext, uartDevices, onName }: {
  registry: ProviderRegistry
  routing: RoutingState
  pinContext: PinContext
  uartDevices: readonly { readonly id: number; readonly name: string }[]
  onName: (id: number, name: string) => void
}) {
  const modules = useMemo(() => standardModulePorts(routing.link.defs), [routing.link.defs])
  const { providers, sessions, links } = routing.snapshot
  const ownId = routing.link.session?.clientId
  return (
    <div className="project-screen connections">
      <div className="project-actions">
        <Button size="sm" disabled={!routing.connected || !!routing.busy} onClick={() => void routing.refresh()}>
          <RefreshCw aria-hidden="true" className={routing.busy ? 'spinning' : undefined} />Refresh
        </Button>
        {routing.busy && <span className="bar-note" role="status">{routing.busy}…</span>}
      </div>
      {!routing.connected && <p className="project-note">Not connected: these are the board&apos;s providers as it starts. Connect the board to change them.</p>}
      {routing.note && <p className="routing-note">{routing.note}</p>}
      <FieldError>{routing.error}</FieldError>
      <CardStack>
        <ErrorsCard about={`router_key:${routing.link.defs.keys.provider}`} />
        {providers.map((p) => {
          const session = sessions.find((s) => s.provider === p.id)
          return (
            <ProviderCard key={p.id} registry={registry} provider={p} {...(session ? { session } : {})} others={providers.filter((o) => o.id !== p.id)} links={links} modules={modules} routing={routing}
              own={session?.open === true && session.peerId === ownId} onName={onName} />
          )
        })}
        <CreateProviderCard registry={registry} routing={routing} pinContext={pinContext} uartDevices={uartDevices} onName={onName} />
      </CardStack>
    </div>
  )
}
