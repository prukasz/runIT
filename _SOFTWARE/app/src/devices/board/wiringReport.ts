import { useMemo } from 'react'
import { deviceDisplayName } from '../../domain/devices'
import { buildNets, checkWiring, configLinks, netContext, netVoltage, specOf } from '../../domain/hardware'
import type { ConfigLink, PinSpec, WiringContext, WiringIssue, WiringNet } from '../../domain/hardware'
import type { DevicesWorkspace } from '../useDevicesWorkspace'
import { RUNIT_MODEL, buildBoardModel } from './boardModel'
import type { BoardModel } from './boardModel'
import { wiringContext, wiringDevices } from './pinSpecs'

/*
 * The wiring of the project's devices, evaluated: every wire's net, the issues the checks raise (by wire and by
 * device) and the install fields the wires imply. Shared by the canvas (wire colours, voltages, problems) and the
 * details panel.
 */

export interface WiringReport {
  readonly context: WiringContext
  readonly issues: readonly WiringIssue[]
  readonly nets: readonly WiringNet[]
  readonly netOfWire: ReadonlyMap<string, WiringNet>
  readonly issuesOfWire: ReadonlyMap<string, readonly WiringIssue[]>
  readonly issuesOfDevice: ReadonlyMap<string, readonly WiringIssue[]>
  readonly links: readonly ConfigLink[]
  readonly pinsOf: (device: string) => ReadonlyMap<string, PinSpec> | undefined
}

const group = <K>(issues: readonly WiringIssue[], key: (issue: WiringIssue) => K | undefined): Map<K, WiringIssue[]> => {
  const map = new Map<K, WiringIssue[]>()
  for (const issue of issues) {
    const at = key(issue)
    if (at !== undefined) map.set(at, [...(map.get(at) ?? []), issue])
  }
  return map
}

export function evaluateWiring(w: Pick<DevicesWorkspace, 'devices' | 'catalog' | 'boardLayout' | 'wires'> & { readonly board?: DevicesWorkspace['board'] }, model: BoardModel = w.board ? buildBoardModel(w.board, w.catalog) : RUNIT_MODEL): WiringReport {
  const devices = wiringDevices(w.devices, (id) => w.catalog.type(id), w.boardLayout, (device) => deviceDisplayName(w.catalog, device))
  const context = wiringContext(devices, model)
  const issues = checkWiring(w.wires, context)
  // Nets of the wires whose two ends exist (a wire to a missing pin is reported, not drawn into a net).
  const usable = w.wires.filter((wire) => specOf(context, wire.a) && specOf(context, wire.b))
  const nets = buildNets(usable, netContext(context))
  const netOfWire = new Map<string, WiringNet>()
  for (const net of nets) for (const id of net.wires) netOfWire.set(id, net)
  const byDevice = new Map(devices.map((device) => [device.ref, device.pins]))
  return {
    context,
    issues,
    nets,
    netOfWire,
    issuesOfWire: group(issues, (issue) => issue.wire),
    issuesOfDevice: group(issues, (issue) => issue.device),
    links: configLinks(usable, context),
    pinsOf: (device) => byDevice.get(device),
  }
}

export const useWiringReport = (w: Pick<DevicesWorkspace, 'devices' | 'catalog' | 'boardLayout' | 'wires'>, model: BoardModel): WiringReport =>
  useMemo(() => evaluateWiring(w, model), [w.devices, w.catalog, w.boardLayout, w.wires, model]) // eslint-disable-line react-hooks/exhaustive-deps

const volts = (mV: number): string => `${Number((mV / 1000).toFixed(2))} V`

/** What a net's wires are labelled with: its voltage, or what sets it. Undefined for a signal. */
export function netLabel(net: WiringNet, report: WiringReport): string | undefined {
  if (net.role === 'ground') return 'GND'
  if (net.role !== 'power') return undefined
  const voltage = netVoltage(net, report.context.nets)
  if (voltage) return voltage.min === voltage.max ? volts(voltage.min) : `${Number((voltage.min / 1000).toFixed(2))}–${volts(voltage.max)}`
  const names = net.named.map((name) => report.context.nets[name]?.label ?? name)
  return names.length ? `${names.join(' / ')} (set at runtime)` : undefined
}
