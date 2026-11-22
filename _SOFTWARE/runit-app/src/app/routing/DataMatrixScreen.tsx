import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { ArrowLeftRight, Link2, Maximize2, Plus, RefreshCw, Unlink, ZoomIn, ZoomOut } from 'lucide-react'
import { Badge, Button, Card, ChoiceField, FieldError, FormGrid, FormRow, NumberField, PanelHeader, Switch, ToggleChip } from '@ui/components'
import type { Choice } from '@ui/components'
import { buildGraphLayout, describePort, parsePort, standardModulePorts, wirePath } from './routing'
import type { GraphEdge } from './routing'
import type { RoutingState } from './useRouting'
import './Routing.css'

interface PendingWire {
  readonly fromPort: number
  readonly x1: number
  readonly y1: number
  readonly label: string
}

export function DataMatrixScreen({ routing, onOpenProviders }: {
  routing: RoutingState
  onOpenProviders: () => void
}) {
  const defs = routing.link.defs
  const layout = useMemo(() => buildGraphLayout(routing.snapshot, defs), [routing.snapshot, defs])
  const allModules = useMemo(() => standardModulePorts(defs), [defs])

  // Pan & zoom
  const stageRef = useRef<HTMLDivElement | null>(null)
  const [view, setView] = useState({ x: 24, y: 12, zoom: 0.92 })
  const [panning, setPanning] = useState<{ startX: number; startY: number; origX: number; origY: number } | undefined>()

  // Interactive wiring & selection
  const [pending, setPending] = useState<PendingWire | undefined>()
  const [pointer, setPointer] = useState<{ x: number; y: number }>({ x: 0, y: 0 })
  const [bothWays, setBothWays] = useState<boolean>(false)
  const [selectedEdgeId, setSelectedEdgeId] = useState<string | undefined>()
  const [hoveredNodeKey, setHoveredNodeKey] = useState<string | undefined>()

  // Attach session drawer state
  const [attachOpen, setAttachOpen] = useState<boolean>(false)
  const [attachProvider, setAttachProvider] = useState<number | undefined>()
  const [attachDeadman, setAttachDeadman] = useState<number>(0)
  const [attachSafety, setAttachSafety] = useState<boolean>(false)
  const [attachModules, setAttachModules] = useState<number[]>(() => [defs.keys.router!, defs.keys.errors!, defs.keys.sys_event!])

  const attachableProviders = useMemo(
    () => layout.providers.filter((p) => p.provider.framed && !p.hasSession && p.provider.id < 128),
    [layout.providers],
  )
  const attachChoices: Choice<number>[] = useMemo(
    () => attachableProviders.map((p) => ({ value: p.provider.id, label: `Provider #${p.provider.id} (${p.provider.typeLabel})` })),
    [attachableProviders],
  )

  const selectedEdge: GraphEdge | undefined = useMemo(
    () => layout.edges.find((e) => e.id === selectedEdgeId),
    [layout.edges, selectedEdgeId],
  )

  const fitView = useCallback(() => {
    const el = stageRef.current
    if (!el) return
    const rect = el.getBoundingClientRect()
    if (rect.width <= 0 || rect.height <= 0) return
    const zx = (rect.width - 32) / layout.width
    const zy = (rect.height - 32) / layout.height
    const zoom = Math.max(0.45, Math.min(1.15, Math.min(zx, zy)))
    const x = Math.max(16, (rect.width - layout.width * zoom) / 2)
    const y = Math.max(12, (rect.height - layout.height * zoom) / 2)
    setView({ x, y, zoom })
  }, [layout.width, layout.height])

  useEffect(() => {
    fitView()
  }, [fitView])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setPending(undefined)
        setSelectedEdgeId(undefined)
        setAttachOpen(false)
      } else if ((e.key === 'Delete' || e.key === 'Backspace') && selectedEdge && routing.connected && !routing.busy) {
        const tag = (e.target as HTMLElement | null)?.tagName
        if (tag === 'INPUT' || tag === 'TEXTAREA') return
        e.preventDefault()
        void routing.disconnectPorts(selectedEdge.from, selectedEdge.to).then((ok) => {
          if (ok) setSelectedEdgeId(undefined)
        })
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [selectedEdge, routing])

  const toWorld = (clientX: number, clientY: number) => {
    const rect = stageRef.current?.getBoundingClientRect()
    if (!rect) return { x: 0, y: 0 }
    return { x: (clientX - rect.left - view.x) / view.zoom, y: (clientY - rect.top - view.y) / view.zoom }
  }

  const startWire = (portId: number, x: number, y: number) => {
    setSelectedEdgeId(undefined)
    setPointer({ x, y })
    setPending({ fromPort: portId, x1: x, y1: y, label: describePort(portId, routing.snapshot.providers, defs) })
  }

  const finishWire = async (toPort: number, shiftKey: boolean) => {
    if (!pending) return
    const fromPort = pending.fromPort
    setPending(undefined)
    if (fromPort === toPort) return
    await routing.connectPorts(fromPort, toPort, bothWays || shiftKey)
  }

  const openAttachFor = (providerId?: number) => {
    const target = providerId ?? attachableProviders[0]?.provider.id
    setAttachProvider(target)
    setAttachOpen(true)
  }

  const toggleModule = (modId: number) => {
    setAttachModules((prev) => (prev.includes(modId) ? prev.filter((x) => x !== modId) : [...prev, modId]))
  }

  const handleAttach = async () => {
    if (attachProvider === undefined || attachModules.length === 0) return
    const ok = await routing.attachSession(attachProvider, attachDeadman, attachSafety, attachModules)
    if (ok) setAttachOpen(false)
  }

  const edgeMatchesHover = (e: GraphEdge): boolean => {
    if (!hoveredNodeKey) return true
    const pf = layout.pins.get(e.from)?.nodeKey
    const pt = layout.pins.get(e.to)?.nodeKey
    return pf === hoveredNodeKey || pt === hoveredNodeKey
  }

  const activeClientProvider = useMemo(() => {
    const myId = routing.link.session?.clientId
    if (myId === undefined) return undefined
    return routing.snapshot.sessions.find((s) => s.open && s.peerId === myId)?.provider
  }, [routing.link.session, routing.snapshot.sessions])

  const connectedCount = layout.modules.filter((m) => m.connected).length + layout.providers.filter((p) => p.connected).length
  const totalEndpoints = layout.modules.length + layout.providers.length

  return (
    <div className="dm-screen">
      <div className="dm-bar">
        <PanelHeader title="Data matrix">
          <Badge tone={routing.live ? 'success' : 'neutral'} size="compact">
            {routing.live ? 'live' : 'preview'}
          </Badge>
          <span className="dm-stats">
            {connectedCount}/{totalEndpoints} wired · {layout.edges.length} links · {layout.sessions.length} sessions
          </span>
        </PanelHeader>

        <div className="dm-bar-actions">
          <ToggleChip selected={bothWays} onClick={() => setBothWays(!bothWays)} title="When active (or holding Shift), connecting two ports creates links in both directions">
            <ArrowLeftRight aria-hidden="true" />Both ways
          </ToggleChip>
          <Button size="sm" onClick={onOpenProviders}><Plus aria-hidden="true" />Provider</Button>
          <Button size="sm" variant="primary" title={attachableProviders.length === 0 ? 'Create a framed provider (ID 1..127) without a session first' : 'Attach a session to a framed provider'} onClick={() => openAttachFor()}>
            <Link2 aria-hidden="true" />Attach session
          </Button>
          <Button size="sm" title="Zoom out" onClick={() => setView((v) => ({ ...v, zoom: Math.max(0.4, v.zoom - 0.12) }))}><ZoomOut aria-hidden="true" /></Button>
          <Button size="sm" title="Zoom in" onClick={() => setView((v) => ({ ...v, zoom: Math.min(1.6, v.zoom + 0.12) }))}><ZoomIn aria-hidden="true" /></Button>
          <Button size="sm" title="Fit graph to view" onClick={fitView}><Maximize2 aria-hidden="true" /></Button>
          <Button size="sm" disabled={!routing.connected || !!routing.busy} onClick={() => void routing.refresh()}>
            <RefreshCw aria-hidden="true" className={routing.busy ? 'spinning' : undefined} />Refresh
          </Button>
        </div>
      </div>

      {(routing.error || routing.note || pending || selectedEdge || !routing.connected) && (
        <div className="dm-status-strip">
          {!routing.connected && <span className="dm-hint">Offline preview of default boot topology. Connect the board to inspect and edit live links.</span>}
          {pending && (
            <span className="dm-hint is-active">
              Wiring from <strong>{pending.label}</strong> — click any highlighted IN handle to connect (Esc to cancel)
            </span>
          )}
          {selectedEdge && !pending && (
            <span className="dm-selected-edge">
              <span>Link: <strong>{describePort(selectedEdge.from, routing.snapshot.providers, defs)}</strong> → <strong>{describePort(selectedEdge.to, routing.snapshot.providers, defs)}</strong></span>
              <Button size="sm" variant="danger" disabled={!routing.connected || !!routing.busy} onClick={() => void routing.disconnectPorts(selectedEdge.from, selectedEdge.to).then((ok) => ok && setSelectedEdgeId(undefined))}>
                <Unlink aria-hidden="true" />Disconnect
              </Button>
            </span>
          )}
          {routing.note && !pending && !selectedEdge && <span className="dm-hint is-ok">{routing.note}</span>}
          {routing.error && <FieldError>{routing.error}</FieldError>}
        </div>
      )}

      {attachOpen && (
        <div className="dm-drawer">
          <Card
            title="Attach session to provider"
            help="Wraps a framed provider in a runIT session (SESSION ATTACH): automatically links provider <-> session line and exposes one sub-port per selected module."
            actions={
              <span className="settings-actions">
                <Button size="sm" onClick={() => setAttachOpen(false)}>Cancel</Button>
                <Button size="sm" variant="primary" disabled={!routing.connected || !!routing.busy || attachProvider === undefined || attachModules.length === 0} onClick={() => void handleAttach()}>
                  <Link2 aria-hidden="true" />Attach
                </Button>
              </span>
            }
          >
            <FormGrid>
              <FormRow label="Framed provider" note="Only framed providers (ID 1..127) without a session are listed.">
                <ChoiceField<number> aria-label="Framed provider" value={attachProvider} choices={attachChoices} onChange={setAttachProvider} />
              </FormRow>
              <FormRow label="Dead-man (ms)" note="0 = off; otherwise 150..60000 ms silence timeout.">
                <NumberField aria-label="Dead-man timeout" integer min={0} max={60000} value={attachDeadman} onCommit={setAttachDeadman} />
              </FormRow>
              <FormRow label="Safety session" note="If enabled, losing this session enters the board's safe state.">
                <Switch label={attachSafety ? 'Safety' : 'Normal'} checked={attachSafety} onChange={setAttachSafety} />
              </FormRow>
            </FormGrid>
            <div className="dm-module-picks">
              <span className="dm-module-picks-label">Sub-port modules:</span>
              {allModules.map((m) => (
                <ToggleChip key={m.moduleId} selected={attachModules.includes(m.moduleId)} onClick={() => toggleModule(m.moduleId)}>
                  {m.name}
                </ToggleChip>
              ))}
            </div>
          </Card>
        </div>
      )}

      <div
        ref={stageRef}
        className={`dm-stage ${panning ? 'is-panning' : ''} ${pending ? 'is-wiring' : ''}`}
        onPointerDown={(e) => {
          if (e.button !== 0) return
          if ((e.target as HTMLElement).closest('[data-interactive]')) return
          setPending(undefined)
          setSelectedEdgeId(undefined)
          setPanning({ startX: e.clientX, startY: e.clientY, origX: view.x, origY: view.y })
        }}
        onPointerMove={(e) => {
          if (panning) {
            setView((v) => ({ ...v, x: panning.origX + (e.clientX - panning.startX), y: panning.origY + (e.clientY - panning.startY) }))
          }
          if (pending) {
            setPointer(toWorld(e.clientX, e.clientY))
          }
        }}
        onPointerUp={() => setPanning(undefined)}
        onWheel={(e) => {
          e.preventDefault()
          const factor = e.deltaY < 0 ? 1.08 : 0.92
          const rect = stageRef.current?.getBoundingClientRect()
          if (!rect) return
          const mx = e.clientX - rect.left
          const my = e.clientY - rect.top
          setView((v) => {
            const next = Math.max(0.4, Math.min(1.6, v.zoom * factor))
            const k = next / v.zoom
            return { zoom: next, x: mx - (mx - v.x) * k, y: my - (my - v.y) * k }
          })
        }}
      >
        <svg className="dm-svg" width="100%" height="100%">
          <defs>
            {(['module-session', 'session-provider', 'direct', 'bridge', 'selected', 'pending'] as const).map((kind) => (
              <marker key={kind} id={`dm-arrow-${kind}`} viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
                <path d="M 0 1.5 L 8.5 5 L 0 8.5 z" className={`dm-arrow dm-arrow-${kind}`} />
              </marker>
            ))}
          </defs>

          <g transform={`translate(${view.x}, ${view.y}) scale(${view.zoom})`}>
            {/* Column headings */}
            <text x={36} y={28} className="dm-col-title">MODULES</text>
            <text x={370} y={28} className="dm-col-title">SESSIONS</text>
            <text x={760} y={28} className="dm-col-title">PROVIDERS</text>

            {/* Wires */}
            {layout.edges.map((e) => {
              const selected = e.id === selectedEdgeId
              const lit = edgeMatchesHover(e)
              const pf = parsePort(e.from)
              const pt = parsePort(e.to)
              const sameSide = (pf.kind === 'provider' && pt.kind === 'provider') || (pf.kind === 'module' && pt.kind === 'module')
              const d = wirePath(e.x1, e.y1, e.x2, e.y2, sameSide)
              const marker = selected ? 'selected' : e.kind
              return (
                <g key={e.id} data-interactive="edge" className={`dm-edge dm-edge-${e.kind} ${selected ? 'is-selected' : ''} ${!lit ? 'is-dim' : ''}`}
                  onClick={(ev) => {
                    ev.stopPropagation()
                    setPending(undefined)
                    setSelectedEdgeId(selected ? undefined : e.id)
                  }}>
                  <path d={d} className="dm-edge-hit" />
                  <path d={d} className="dm-edge-line" markerEnd={`url(#dm-arrow-${marker})`} />
                </g>
              )
            })}

            {/* Live rubber-band wire while connecting */}
            {pending && (
              <path
                d={wirePath(pending.x1, pending.y1, pointer.x, pointer.y)}
                className="dm-edge-pending"
                markerEnd="url(#dm-arrow-pending)"
              />
            )}

            {/* Column 1: Module nodes */}
            {layout.modules.map((m) => {
              const nodeKey = `mod:${m.spec.moduleId}`
              const canDrop = !!pending && m.inPin !== undefined && pending.fromPort !== m.portId
              return (
                <g key={m.portId} className={`dm-node dm-node-module ${m.connected ? 'is-wired' : 'is-unwired'}`}
                  onMouseEnter={() => setHoveredNodeKey(nodeKey)} onMouseLeave={() => setHoveredNodeKey(undefined)}>
                  <rect x={m.x} y={m.y} width={m.w} height={m.h} rx={8} className="dm-node-box" />
                  <text x={m.x + 12} y={m.y + 24} className="dm-node-title">{m.spec.name}</text>
                  <text x={m.x + 12} y={m.y + 42} className="dm-node-sub">
                    {m.connected ? `0x${m.spec.moduleId.toString(16).toUpperCase().padStart(2, '0')}` : 'unconnected'}
                  </text>
                  {m.inPin && (
                    <g data-interactive="pin" className={`dm-pin dm-pin-in ${canDrop ? 'is-target' : ''}`}
                      onClick={(ev) => { ev.stopPropagation(); if (pending) void finishWire(m.portId, ev.shiftKey) }}>
                      <title>{`IN: ${m.spec.name}`}</title>
                      <text x={m.inPin.x - 14} y={m.inPin.y + 3} textAnchor="end" className="dm-pin-label">IN</text>
                      <circle cx={m.inPin.x} cy={m.inPin.y} r={6} className="dm-pin-dot" />
                    </g>
                  )}
                  {m.outPin && (
                    <g data-interactive="pin" className={`dm-pin dm-pin-out ${pending?.fromPort === m.portId ? 'is-origin' : ''}`}
                      onClick={(ev) => { ev.stopPropagation(); if (!pending) startWire(m.portId, m.outPin!.x, m.outPin!.y) }}>
                      <title>{`OUT: ${m.spec.name} (click to wire)`}</title>
                      <text x={m.outPin.x - 14} y={m.outPin.y + 3} textAnchor="end" className="dm-pin-label">OUT</text>
                      <circle cx={m.outPin.x} cy={m.outPin.y} r={6} className="dm-pin-dot" />
                    </g>
                  )}
                </g>
              )
            })}

            {/* Column 2: Session nodes */}
            {layout.sessions.map((s) => {
              const nodeKey = `ses:${s.session.provider}`
              const isOwnLink = s.session.provider === activeClientProvider
              const peerText = s.session.open ? `peer 0x${(s.session.peerId ?? 0).toString(16).toUpperCase()}` : 'waiting hello'
              return (
                <g key={s.session.provider} className={`dm-node dm-node-session ${s.session.open ? 'is-open' : ''}`}
                  onMouseEnter={() => setHoveredNodeKey(nodeKey)} onMouseLeave={() => setHoveredNodeKey(undefined)}>
                  <rect x={s.x} y={s.y} width={s.w} height={s.h} rx={10} className="dm-node-box" />
                  <rect x={s.x} y={s.y} width={s.w} height={44} rx={10} className="dm-session-head-bg" />
                  <text x={s.x + 12} y={s.y + 19} className="dm-node-title">Session · Provider #{s.session.provider}</text>
                  <text x={s.x + 12} y={s.y + 35} className="dm-node-sub">
                    {peerText}{s.session.deadmanMs > 0 ? ` · dm ${s.session.deadmanMs}ms` : ''}{s.session.safety ? ' · safety' : ''}
                  </text>
                  {routing.connected && !isOwnLink && (
                    <g data-interactive="action" className="dm-svg-btn dm-svg-btn-danger"
                      onClick={(ev) => { ev.stopPropagation(); void routing.detachSession(s.session.provider) }}>
                      <title>{`Detach session from provider #${s.session.provider}`}</title>
                      <rect x={s.x + s.w - 64} y={s.y + 8} width={54} height={20} rx={4} />
                      <text x={s.x + s.w - 37} y={s.y + 21} textAnchor="middle">Detach</text>
                    </g>
                  )}
                  {/* Session line pin (right side -> provider) */}
                  <g data-interactive="pin" className="dm-pin dm-pin-line">
                    <title>{`Session #${s.session.provider} line port`}</title>
                    <circle cx={s.linePin.x} cy={s.linePin.y} r={6} className="dm-pin-dot" />
                  </g>
                  {/* Sub-ports (left side -> modules) */}
                  {s.subRows.length === 0 ? (
                    <text x={s.x + 14} y={s.y + 66} className="dm-node-sub">No module links yet</text>
                  ) : (
                    s.subRows.map((row) => {
                      const canDrop = !!pending && pending.fromPort !== row.portId
                      return (
                        <g key={row.portId}>
                          <text x={s.x + 18} y={row.y + 4} className="dm-sub-label">{row.label}</text>
                          <g data-interactive="pin" className={`dm-pin dm-pin-out ${pending?.fromPort === row.portId ? 'is-origin' : ''}`}
                            onClick={(ev) => { ev.stopPropagation(); if (!pending) startWire(row.portId, row.outPin.x, row.outPin.y) }}>
                            <title>{`OUT: Session #${s.session.provider} · ${row.label}`}</title>
                            <circle cx={row.outPin.x} cy={row.outPin.y} r={4.5} className="dm-pin-dot" />
                          </g>
                          <g data-interactive="pin" className={`dm-pin dm-pin-in ${canDrop ? 'is-target' : ''}`}
                            onClick={(ev) => { ev.stopPropagation(); if (pending) void finishWire(row.portId, ev.shiftKey) }}>
                            <title>{`IN: Session #${s.session.provider} · ${row.label}`}</title>
                            <circle cx={row.inPin.x} cy={row.inPin.y} r={4.5} className="dm-pin-dot" />
                          </g>
                        </g>
                      )
                    })
                  )}
                </g>
              )
            })}

            {/* Column 3: Provider nodes */}
            {layout.providers.map((p) => {
              const nodeKey = `prv:${p.provider.id}`
              const canDrop = !!pending && pending.fromPort !== p.portId
              const canAttach = p.provider.framed && !p.hasSession && p.provider.id < 128
              const flags = [p.provider.up ? 'up' : 'down', p.provider.framed ? 'framed' : 'raw', ...(p.provider.wired ? ['wired'] : []), ...(p.provider.system ? ['sys'] : [])].join(' · ')
              return (
                <g key={p.portId} className={`dm-node dm-node-provider ${p.connected ? 'is-wired' : 'is-unwired'} ${p.provider.up ? 'is-up' : ''}`}
                  onMouseEnter={() => setHoveredNodeKey(nodeKey)} onMouseLeave={() => setHoveredNodeKey(undefined)}>
                  <rect x={p.x} y={p.y} width={p.w} height={p.h} rx={8} className="dm-node-box" />
                  <line x1={p.x + 42} y1={p.y + 10} x2={p.x + 42} y2={p.y + p.h - 10} stroke="currentColor" strokeOpacity={0.08} />
                  <text x={p.x + 52} y={p.y + 25} className="dm-node-title">#{p.provider.id} · {p.provider.typeLabel}</text>
                  <text x={p.x + 52} y={p.y + 44} className="dm-node-sub">{flags}</text>
                  <text x={p.x + 52} y={p.y + 66} className={`dm-node-state ${p.connected ? 'is-wired' : 'is-unwired'}`}>
                    {p.connected ? 'connected' : 'unconnected'}
                  </text>
                  {canAttach && (
                    <g data-interactive="action" className="dm-svg-btn"
                      onClick={(ev) => { ev.stopPropagation(); openAttachFor(p.provider.id) }}>
                      <title>{`Attach a session on provider #${p.provider.id}`}</title>
                      <rect x={p.x + p.w - 84} y={p.y + 54} width={74} height={22} rx={4} />
                      <text x={p.x + p.w - 47} y={p.y + 69} textAnchor="middle">+ Session</text>
                    </g>
                  )}
                  <g data-interactive="pin" className={`dm-pin dm-pin-in ${canDrop ? 'is-target' : ''}`}
                    onClick={(ev) => { ev.stopPropagation(); if (pending) void finishWire(p.portId, ev.shiftKey) }}>
                    <title>{`IN: Provider #${p.provider.id}`}</title>
                    <text x={p.inPin.x + 12} y={p.inPin.y + 3} className="dm-pin-label">IN</text>
                    <circle cx={p.inPin.x} cy={p.inPin.y} r={6} className="dm-pin-dot" />
                  </g>
                  <g data-interactive="pin" className={`dm-pin dm-pin-out ${pending?.fromPort === p.portId ? 'is-origin' : ''}`}
                    onClick={(ev) => { ev.stopPropagation(); if (!pending) startWire(p.portId, p.outPin.x, p.outPin.y) }}>
                    <title>{`OUT: Provider #${p.provider.id} (click to wire)`}</title>
                    <text x={p.outPin.x + 12} y={p.outPin.y + 3} className="dm-pin-label">OUT</text>
                    <circle cx={p.outPin.x} cy={p.outPin.y} r={6} className="dm-pin-dot" />
                  </g>
                </g>
              )
            })}
          </g>
        </svg>
      </div>

      <div className="dm-legend">
        <span className="dm-legend-item"><i className="dm-swatch dm-swatch-ms" />Module ↔ Session</span>
        <span className="dm-legend-item"><i className="dm-swatch dm-swatch-sp" />Session ↔ Provider</span>
        <span className="dm-legend-item"><i className="dm-swatch dm-swatch-dir" />Direct stream</span>
        <span className="dm-legend-item"><i className="dm-swatch dm-swatch-br" />Provider bridge</span>
        <span className="dm-legend-spacer" />
        <span className="dm-legend-tip">Click OUT → IN to connect · Click wire + Delete to disconnect · Drag to pan · Scroll to zoom</span>
      </div>
    </div>
  )
}
