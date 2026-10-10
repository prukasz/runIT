import { Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Button } from '../components/Button'
import { Card } from '../components/Card'
import { FLOW_SHORT, flowDirection, flowOf, labelOf, otherEnd, specOf, touches } from '../domain/hardware'
import type { Wire, WiringIssue } from '../domain/hardware'
import { installLink, installWith, modeFix } from './board/installLinks'
import { netLabel } from './board/wiringReport'
import type { WiringReport } from './board/wiringReport'
import type { BoardWiring } from './useBoardWiring'
import type { DevicesWorkspace } from './useDevicesWorkspace'
import './Devices.css'

/*
 * The wiring in the Board canvas's right panel: the selected wire, the selected device's connections and the install
 * fields they imply, and every problem the checks found. Built from `evaluateWiring` (board/wiringReport.ts).
 */

const TONE = { error: 'danger', warning: 'warning', info: 'info' } as const
const FLOW_TONE = { producer: 'accent', consumer: 'neutral', both: 'other' } as const

/** A pin's role as a small flag. */
export function FlowBadge({ spec }: { spec: Parameters<typeof flowOf>[0] | undefined }) {
  if (!spec) return null
  const flow = flowOf(spec)
  return <Badge tone={FLOW_TONE[flow]} size="compact" caps title={flow === 'both' ? 'Can produce or consume: it takes the role the other end needs' : flow === 'producer' ? 'Produces (drives) the signal' : 'Consumes (receives) the signal'}>{FLOW_SHORT[flow]}</Badge>
}

function Issue({ issue, onClick, workspace: w, report }: { issue: WiringIssue; onClick?: () => void; workspace: DevicesWorkspace; report: WiringReport }) {
  // "Set it as an output / input" is a note with a fix: the pin's default mode. Once the setup holds it the note is done.
  const fix = modeFix(issue, report.context, w.catalog, w.setup)
  if (fix?.applied) return null
  return (
    <li className={`board-issue is-${issue.severity}`}>
      <Badge tone={TONE[issue.severity]} size="compact" caps>{issue.severity}</Badge>
      {onClick ? <button type="button" className="board-issue-text" onClick={onClick}>{issue.message}</button> : <span className="board-issue-text">{issue.message}</span>}
      {fix && <Button size="sm" className="board-issue-fix" title="Add this to the pin's default settings" onClick={() => w.setPinMode(fix.ref, fix.pin, fix.mode)}>Set {fix.label}</Button>}
    </li>
  )
}

/** The selected wire: its ends, its net and what is wrong with it. */
export function WireCard({ workspace: w, report, wiring, wire }: { workspace: DevicesWorkspace; report: WiringReport; wiring: BoardWiring; wire: Wire }) {
  const net = report.netOfWire.get(wire.id)
  const label = net ? netLabel(net, report) : undefined
  const issues = [...(report.issuesOfWire.get(wire.id) ?? []), ...report.issues.filter((issue) => issue.net && issue.net === net?.id && !issue.wire)]
  return (
    <Card title="Wire" className="board-wire-card">
      {(() => {
        const specA = specOf(report.context, wire.a)
        const specB = specOf(report.context, wire.b)
        const direction = specA && specB ? flowDirection(specA, specB) : undefined
        const [from, to] = direction === 'ba' ? [wire.b, wire.a] : [wire.a, wire.b]
        return (
          <p className="board-wire-ends">
            <strong>{labelOf(report.context, from)}</strong> <FlowBadge spec={specOf(report.context, from)} /> {direction ? '→' : '↔'} <strong>{labelOf(report.context, to)}</strong> <FlowBadge spec={specOf(report.context, to)} />
          </p>
        )
      })()}
      <div className="board-wire-facts">
        {net && <Badge size="compact">{net.role}</Badge>}
        {label && <Badge tone="number" size="compact">{label}</Badge>}
        {net && <Badge tone="count" size="compact">{net.endpoints.length} pins on the net</Badge>}
      </div>
      {issues.length > 0 && <ul className="board-issues">{issues.map((issue, index) => <Issue key={index} issue={issue} workspace={w} report={report} />)}</ul>}
      <Button size="sm" variant="danger" onClick={() => { w.removeWire(wire.id); wiring.selectWire(undefined) }}><Trash2 aria-hidden="true" />Delete wire</Button>
    </Card>
  )
}

/** The selected device's connections, the install fields they imply, and its problems. */
export function DeviceWiring({ workspace: w, report, wiring, deviceRef }: { workspace: DevicesWorkspace; report: WiringReport; wiring: BoardWiring; deviceRef: string }) {
  const mine = w.wires.filter((wire) => wire.a.device === deviceRef || wire.b.device === deviceRef)
  const pins = report.pinsOf(deviceRef)
  const links = report.links.filter((link) => link.device === deviceRef).map((link) => installLink(link, w.devices, w.catalog)).filter((entry) => !!entry)
  const issues = report.issuesOfDevice.get(deviceRef) ?? []

  return (
    <Card title="Wiring" className="board-device-wiring">
      {!mine.length && <p className="devices-muted">No wires yet. Press a pin and drag to a board terminal or another device's pin.{pins ? ` ${pins.size} pins can be wired.` : ''}</p>}
      {mine.length > 0 && (
        <ul className="board-connections">
          {mine.map((wire) => {
            const own = wire.a.device === deviceRef ? wire.a : wire.b
            const far = otherEnd(wire, own)
            const bad = (report.issuesOfWire.get(wire.id) ?? []).some((issue) => issue.severity === 'error')
            return (
              <li key={wire.id} className={wiring.selectedWire === wire.id ? 'is-selected' : ''}>
                <button type="button" className="board-connection" onClick={() => wiring.selectWire(wire.id)} title="Select this wire">
                  <span className="board-connection-pin">{report.context.devices.find((device) => device.ref === deviceRef)?.pins.get(own.pin)?.label ?? own.pin}</span>
                  <FlowBadge spec={specOf(report.context, own)} />
                  <span className="board-connection-arrow" aria-hidden="true">→</span>
                  <span className="board-connection-far">{labelOf(report.context, far)}</span>
                  <FlowBadge spec={specOf(report.context, far)} />
                  {bad && <Badge tone="danger" size="compact">error</Badge>}
                </button>
                <button type="button" className="tree-slab-action" aria-label={`Remove the wire to ${labelOf(report.context, far)}`} title="Remove this wire" onClick={() => { w.removeWire(wire.id); if (wiring.selectedWire === wire.id) wiring.selectWire(undefined) }}><Trash2 aria-hidden="true" /></button>
              </li>
            )
          })}
        </ul>
      )}

      {links.length > 0 && (
        <div className="board-install-links">
          <h4>Install frame</h4>
          <ul>
            {links.map((entry) => entry && (
              <li key={entry.link.wire}>
                <span className="board-install-text">
                  <strong>{entry.group.use}</strong> = {report.context.board.get(entry.link.terminal)?.label ?? entry.link.terminal} (device {entry.link.sysPin.device}, pin {entry.link.sysPin.pin})
                  {entry.state === 'match' && <Badge tone="success" size="compact">matches</Badge>}
                  {entry.state === 'unset' && <Badge tone="info" size="compact">not set</Badge>}
                  {entry.state === 'differs' && <Badge tone="warning" size="compact">differs</Badge>}
                  {entry.state === 'differs' && entry.current && <small>The install frame has device {entry.current.device}, pin {entry.current.pin}.</small>}
                </span>
                {entry.state !== 'match' && <Button size="sm" onClick={() => w.updateDevice(entry.device.id, { install: installWith(entry) })}>Apply</Button>}
              </li>
            ))}
          </ul>
        </div>
      )}

      {issues.length > 0 && <ul className="board-issues">{issues.map((issue, index) => <Issue key={index} issue={issue} workspace={w} report={report} onClick={() => issue.wire && wiring.selectWire(issue.wire)} />)}</ul>}
    </Card>
  )
}

/** Every problem of the whole design; a click goes to the wire or device it is about. */
export function WiringProblems({ workspace: w, report, wiring }: { workspace: DevicesWorkspace; report: WiringReport; wiring: BoardWiring }) {
  if (!w.wires.length) return null
  const counts = { error: 0, warning: 0, info: 0 }
  for (const issue of report.issues) counts[issue.severity]++
  return (
    <Card title="Wiring checks" className="board-wiring-checks">
      <div className="board-wire-facts">
        <Badge tone={counts.error ? 'danger' : 'success'} size="compact">{counts.error} errors</Badge>
        <Badge tone={counts.warning ? 'warning' : 'success'} size="compact">{counts.warning} warnings</Badge>
        {counts.info > 0 && <Badge tone="info" size="compact">{counts.info} notes</Badge>}
      </div>
      {report.issues.length === 0 && <p className="devices-muted">{w.wires.length} {w.wires.length === 1 ? 'wire' : 'wires'}, nothing wrong.</p>}
      {report.issues.length > 0 && (
        <ul className="board-issues">
          {report.issues.map((issue, index) => (
            <Issue
              key={index}
              issue={issue}
              workspace={w}
              report={report}
              onClick={() => {
                if (issue.wire) wiring.selectWire(issue.wire)
                else if (issue.device && issue.device !== 'board') w.select({ kind: 'device', ref: issue.device })
              }}
            />
          ))}
        </ul>
      )}
    </Card>
  )
}

export { touches }
