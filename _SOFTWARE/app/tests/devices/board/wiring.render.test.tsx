import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { defaultInstall } from '../../../src/domain/devices'
import type { Wire } from '../../../src/domain/hardware'
import type { ProjectDevice } from '../../../src/domain/project'
import { BoardWires, distanceToPolyline, wirePath, wirePoints } from '../../../src/devices/board/BoardWires'
import { evaluateWiring, netLabel } from '../../../src/devices/board/wiringReport'
import { installLink, installWith } from '../../../src/devices/board/installLinks'
import { BoardDeviceDetails } from '../../../src/devices/BoardDeviceDetails'
import { useBoardWiring } from '../../../src/devices/useBoardWiring'
import { useDevicesWorkspace } from '../../../src/devices/useDevicesWorkspace'
import type { DevicesWorkspace } from '../../../src/devices/useDevicesWorkspace'

const catalog = runitDeviceCatalog()
const pca = catalog.type('device_pca9685')!
const device: ProjectDevice = { id: 'd1', deviceId: 20, type: pca.id, name: 'arm', tags: [], install: defaultInstall(pca, catalog.i2cBuses.user) }
const w1: Wire = { id: 'w1', a: { device: 'd1', pin: 'VDD' }, b: { device: 'board', pin: 'pwrout6-vs' } }
const w2: Wire = { id: 'w2', a: { device: 'd1', pin: 'OE' }, b: { device: 'board', pin: 'gpio-io46' } }
const w3: Wire = { id: 'w3', a: { device: 'd1', pin: 'VSS' }, b: { device: 'board', pin: 't3v3-gnd' } }

const state = (wires: Wire[]) => ({ devices: [device], catalog, boardLayout: { d1: { x: 40, y: 10, rotation: 0 as const, mirror: false, variant: 'chip' } }, wires })

describe('wiring report', () => {
  it('finds the overvoltage of a 5.5 V pin on the board input, on its wire', () => {
    const report = evaluateWiring(state([w1]))
    expect(report.issues.find((issue) => issue.code === 'overvoltage')?.wire).toBe('w1')
    expect(netLabel(report.netOfWire.get('w1')!, report)).toBe('6–20 V')
  })

  it('labels ground and a runtime-set rail, and finds the install field a wire implies', () => {
    const report = evaluateWiring(state([w2, w3]))
    expect(netLabel(report.netOfWire.get('w3')!, report)).toBe('GND')
    expect(report.links).toHaveLength(1)
    expect(report.links[0]).toMatchObject({ device: 'd1', field: 'oe_pin', sysPin: { device: 0, pin: 46 } })
  })

  it('compares the install frame with the wire: unset, then equal once filled', () => {
    const report = evaluateWiring(state([w2]))
    const entry = installLink(report.links[0], [device], catalog)!
    expect(entry.state).toBe('unset')
    const filled = { ...device, install: installWith(entry) }
    expect(filled.install).toMatchObject({ oe_pin_device_id: 0, oe_pin_pin: 46 })
    expect(installLink(report.links[0], [filled], catalog)?.state).toBe('match')
    expect(installLink(report.links[0], [{ ...device, install: { ...device.install, oe_pin_pin: 3 } }], catalog)?.state).toBe('differs')
  })
})

describe('BoardWires', () => {
  const report = evaluateWiring(state([w1, w3]))
  const positions = new Map([['d1|VDD', { x: 10, y: 10 }], ['board|pwrout6-vs', { x: 200, y: 80 }], ['d1|VSS', { x: 10, y: 30 }], ['board|t3v3-gnd', { x: 200, y: 120 }]])

  function Overlay({ power }: { power: boolean }) {
    const wiring = { ...useBoardWiring(), power }
    return <BoardWires wires={[w1, w3]} positions={positions} report={report} wiring={wiring} fits={new Map()} zoom={1} />
  }

  it('draws a wire per net with its role, its voltage label and its error', () => {
    const html = renderToString(<Overlay power />)
    expect(html).toContain('is-power')
    expect(html).toContain('is-ground')
    expect(html).toContain('has-error')
    expect(html).toContain('6–20 V')
  })

  it('fades power wires while the Power option is off', () => {
    expect(renderToString(<Overlay power={false} />).match(/is-faded/g)).toHaveLength(2)
  })

  it('has curve geometry that can be picked', () => {
    const a = { x: 0, y: 0 }
    const b = { x: 100, y: 40 }
    expect(wirePath(a, b)).toMatch(/^M 0 0 C /)
    const line = wirePoints(a, b)
    expect(line[0]).toEqual(a)
    expect(line.at(-1)).toEqual(b)
    expect(distanceToPolyline(line[12], line)).toBeCloseTo(0, 6)
    expect(distanceToPolyline({ x: 50, y: 200 }, line)).toBeGreaterThan(100)
  })
})

describe('BoardDeviceDetails wiring', () => {
  function Panel({ wires, select }: { wires: Wire[]; select?: boolean }) {
    const base = useDevicesWorkspace()
    const wiring = useBoardWiring()
    const w: DevicesWorkspace = { ...base, ...state(wires), selection: select ? { kind: 'device', ref: 'd1' } : undefined }
    return <BoardDeviceDetails workspace={w} wiring={wiring} onEditComponent={() => {}} onEditBoard={() => {}} />
  }

  it('lists the selected device’s connections, its install frame and the checks', () => {
    const html = renderToString(<Panel wires={[w1, w2]} select />).replace(/<!-- -->/g, '')
    expect(html).toContain('Wiring')
    expect(html).toContain('VDD')
    expect(html).toContain('VS (board)')
    expect(html).toContain('Install frame')
    expect(html).toContain('Output enable')
    expect(html).toContain('Wiring checks')
    expect(html).toContain('accepts up to 5.5 V')
  })

  it('without a selection it explains how to wire and still lists the problems', () => {
    const html = renderToString(<Panel wires={[w1]} />).replace(/<!-- -->/g, '')
    expect(html).toContain('drag to another to wire them')
    expect(html).toContain('Wiring checks')
  })

  it('a design without wires shows no checks card', () => {
    expect(renderToString(<Panel wires={[]} select />)).not.toContain('Wiring checks')
  })
})

describe('mode fix for a pin that can be either', () => {
  const io3toAin0: Wire = { id: 'wb', a: { device: 'board', pin: 'gpio-io3' }, b: { device: 'board', pin: 'ain0' } }
  const report = evaluateWiring(state([io3toAin0]))
  const issue = report.issues.find((entry) => entry.code === 'set-output')!

  it('IO3 into AIN0 is allowed and asks for IO3 to be an output', () => {
    expect(issue).toBeDefined()
    expect(report.issues.map((entry) => entry.code)).not.toContain('incompatible')
    expect(report.issues.map((entry) => entry.code)).not.toContain('no-driver')
  })

  it('the fix is the default output mode of the board GPIO, and is done once the setup holds it', async () => {
    const { modeFix } = await import('../../../src/devices/board/installLinks')
    const fix = modeFix(issue, report.context, catalog, [])!
    expect(fix).toMatchObject({ ref: 'board:0', pin: 3, applied: false })
    expect(fix.label.toLowerCase()).toContain('push')
    const done = [{ id: 's', device: 'board:0', contract: 'packet_sys_io_set_mode_t', values: { pin: 3, mode: fix.mode } }]
    expect(modeFix(issue, report.context, catalog, done)?.applied).toBe(true)
  })

  it('a set-input note asks for the plain input mode', async () => {
    const { modeFix } = await import('../../../src/devices/board/installLinks')
    const listen = evaluateWiring(state([{ id: 'wl', a: { device: 'board', pin: 'pwm0-pwm' }, b: { device: 'board', pin: 'gpio-io46' } }]))
    const note = listen.issues.find((entry) => entry.code === 'set-input')!
    expect(note.pin).toBe('gpio-io46')
    expect(modeFix(note, listen.context, catalog, [])).toMatchObject({ ref: 'board:0', pin: 46 })
  })
})
