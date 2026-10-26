import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledServiceCatalog } from '../../src/backend'
import { emptyProject } from '../../src/app/project/projectFile'
import type { ProjectFile } from '../../src/app/project/projectFile'
import { applyHardware, contextOf, hardwareOf, isCanvasStep, placePart, removeItems } from '../../src/app/hardware/apply'
import type { ApplyContext } from '../../src/app/hardware/apply'
import { boxAround, centreOn, connectorPinPoints, GOLDPIN_PITCH, layLead, placedPins, turnPlacement } from '../../src/app/hardware/geometry'
import { BOARD_ITEM, readHardware } from '../../src/app/hardware/layout'
import type { PinEnd } from '../../src/app/hardware/layout'
import { boardPartFor, bundledLibrary } from '../../src/app/hardware/library'
import { buildNetlist, pinsNear } from '../../src/app/hardware/netlist'
import { pathTo, solvePower } from '../../src/app/hardware/power'
import { canWire } from '../../src/app/hardware/wiring'
import type { Scene } from '../../src/app/hardware/wiring'

const catalog = bundledServiceCatalog()
const board = bundledBoard('pcb')
const library = bundledLibrary()
const a: ApplyContext = { catalog, board, library }

const wire = (project: ProjectFile, from: PinEnd, to: PinEnd): ProjectFile => {
  const hw = hardwareOf(project)
  return applyHardware({ ...project, hardware: { ...hw, wires: [...hw.wires, { id: `w${hw.wires.length + 1}`, a: from, b: to }] } }, a)
}
const scene = (project: ProjectFile): Scene => {
  const ctx = contextOf(project, a)
  const net = buildNetlist(hardwareOf(project), ctx)
  return { net, power: solvePower(net, hardwareOf(project).power), ctx }
}
const verdict = (project: ProjectFile, from: string, to: string) => {
  const s = scene(project)
  return canWire(s, s.net.nodes.get(from)!, s.net.nodes.get(to)!)
}
const place = (project: ProjectFile, id: string) => placePart(project, library.get(id)!, { x: -60, y: 0 }, a)

describe('the hardware library', () => {
  it('has the drawing of the runIT board for board.json', () => {
    const part = boardPartFor(library, board)
    expect(part?.id).toBe('runit_board_r1')
    expect(part?.svg).toContain('viewBox="0 0 86.25 140"')
  })

  it('draws goldpins on a 2.54 mm pitch, whatever the part', () => {
    const header = library.get('runit_board_r1')!.connectors.find((c) => c.id === 'user_i2c')!
    const [p0, p1] = connectorPinPoints(header)
    expect(p1!.y - p0!.y).toBeCloseTo(GOLDPIN_PITCH)
    const pins = placedPins(library.get('pca9685_breakout')!, { x: 0, y: 0, rotation: 90 })
    expect(pins.size).toBe(6 + 2 + 16 * 3)
  })

  it('finds the pins in reach of a point, nearest first, or every pin of one part', () => {
    const { project, item } = place(emptyProject('Arm'), 'servo_sg90')
    const net = buildNetlist(hardwareOf(project), contextOf(project, a))
    const servo = [...net.nodes.values()].filter((n) => n.end.item === item)
    const first = servo[0]!
    const near = pinsNear(net, { x: first.at.x + 0.1, y: first.at.y }, { radius: 1 })
    expect(near[0]?.key).toBe(first.key)
    expect(near.every((n) => Math.hypot(n.at.x - first.at.x - 0.1, n.at.y - first.at.y) <= 1)).toBe(true)
    expect(pinsNear(net, first.at, { item }).map((n) => n.key).sort()).toEqual(servo.map((n) => n.key).sort())
  })

  it('fits everything round the board with the board in the middle', () => {
    const box = boxAround({ x: 50, y: 50 }, [{ x: 0, y: 40, w: 100, h: 20 }, { x: 120, y: 45, w: 10, h: 10 }])
    expect(box).toEqual({ x: -30, y: 40, w: 160, h: 20 })
    const view = centreOn({ x: 0, y: 0, zoom: 2 }, { x: 50, y: 50 }, { w: 400, h: 300 })
    expect({ x: view.x + 50 * view.zoom, y: view.y + 50 * view.zoom }).toEqual({ x: 200, y: 150 })
  })

  it('lays a wired lead towards what it is wired to: the plug across the way, as far out as the lead is long', () => {
    const lead = library.get('motor_dc_tt')!.connectors.find((c) => c.type === 'wire')!
    const from = lead.from!
    const laid = layLead(lead, { x: from.x - 60, y: from.y })
    const [p0, p1] = connectorPinPoints(laid)
    // Pointing left: the plug's pins one above the other, its middle on the way, left of the exit.
    expect(p0!.x).toBeCloseTo(p1!.x)
    expect((p0!.y + p1!.y) / 2).toBeCloseTo(from.y)
    expect(p0!.x).toBeLessThan(from.x - 3)
    // Not wired anywhere new: as the library has it.
    expect(layLead(lead, from)).toBe(lead)
  })

  it('turns a part a quarter either way about its centre', () => {
    const motor = library.get('motor_dc_gear_12v')!
    const at = { x: 10, y: 20, rotation: 0 as const }
    const right = turnPlacement(motor, at, 90)
    expect(right).toEqual({ x: 28.5, y: 1.5, rotation: 90 })
    expect(turnPlacement(motor, right, -90)).toEqual(at)
    expect(turnPlacement(motor, at, -90).rotation).toBe(270)
    expect(hardwareOf(placePart(emptyProject('Arm'), motor, { x: 0, y: 0 }, a, 270).project).items[0]?.rotation).toBe(270)
  })
})

describe('the Devices canvas', () => {
  const base = emptyProject('Arm')

  it('makes a project device for a part that is one, with its preset', () => {
    const { project, item } = place(base, 'servo_sg90')
    const servo = project.devices.at(-1)!
    expect(servo.type).toBe('servo')
    expect(servo.config).toMatchObject({ min_us: 500, max_us: 2400 })
    expect(hardwareOf(project).items.find((i) => i.id === item)?.device).toBe(servo.id)
    const gone = removeItems(project, [item], a)
    expect(gone.devices).toHaveLength(0)
  })

  it('makes a DC motor device for a motor and finds its driver and channel from its leads', () => {
    let { project, item } = place(base, 'motor_dc_tt')
    const motor = project.devices.at(-1)!
    expect(motor.type).toBe('dc_motor')
    expect(motor.config).toMatchObject({ min_pct: 20, ramp_up_ms: 300 })
    // Both leads on OUT3 + OUT4 of driver 1: its full bridge 1, both ways.
    project = wire(project, { item, pin: 'leads.a' }, { item: BOARD_ITEM, pin: 'drv1.out3' })
    project = wire(project, { item, pin: 'leads.b' }, { item: BOARD_ITEM, pin: 'drv1.out4' })
    expect(project.devices.find((d) => d.id === motor.id)?.config).toMatchObject({ bridge: 5, channel: 1, one_way: 0 })
    // One lead on OUT2, the other to ground: half bridge 1, one way.
    const hw = hardwareOf(project)
    project = applyHardware({ ...project, hardware: { ...hw, wires: [] } }, a)
    project = wire(project, { item, pin: 'leads.a' }, { item: BOARD_ITEM, pin: 'drv1.out2' })
    project = wire(project, { item, pin: 'leads.b' }, { item: BOARD_ITEM, pin: 'pwr_out.gnd1' })
    expect(project.devices.find((d) => d.id === motor.id)?.config).toMatchObject({ bridge: 5, channel: 1, one_way: 1 })
  })

  it('wires a servo: its PWM pin goes into its config, its VCC sets Rail A (REG2) to 5 V and locks it', () => {
    let { project, item } = place(base, 'servo_sg90')
    const servo = project.devices.at(-1)!.id
    project = wire(project, { item, pin: 'lead.sig' }, { item: BOARD_ITEM, pin: 'pwm0.pwm' })
    expect(project.devices.at(-1)!.config?.pwm_pin).toEqual({ device: 3, pin: 0 })
    project = wire(project, { item, pin: 'lead.vcc' }, { item: BOARD_ITEM, pin: 'pwm0.vcc' })
    project = wire(project, { item, pin: 'lead.gnd' }, { item: BOARD_ITEM, pin: 'pwm0.gnd' })
    const railA = project.boardDefaults.find((d) => d.device === 10)!
    expect(railA.steps).toEqual([
      { kind: 'device', device: 10, contract: 'POWER_VREG', command: 'VOLTAGE', args: { voltage_mV: 5000 } },
      { kind: 'device', device: 10, contract: 'POWER_VREG', command: 'ENABLE', args: { enable: 1 } },
    ])
    expect(isCanvasStep(project, 10, railA.steps[0]!)).toBe(true)
    const s = scene(project)
    expect(s.power.problems.filter((p) => p.severity === 'error')).toEqual([])
    // The way to the servo: Rail A, its regulator, the board input, an eFuse, USB.
    const node = s.power.nodeOf.get(`${item}:vcc`)!
    expect(pathTo(s.power, node).map((step) => step.element?.element.id)).toEqual(['reg_a', 'efuse_usb', 'src_usb'])
    // Unwired, the rail is the canvas's no more.
    const hw = hardwareOf(project)
    const back = applyHardware({ ...project, hardware: { ...hw, wires: hw.wires.filter((w) => w.a.pin !== 'lead.vcc') } }, a)
    expect(back.boardDefaults.find((d) => d.device === 10)).toBeUndefined()
    expect(servo).toBeGreaterThan(13)
  })

  it('puts no wire on the USB-C socket: it takes a cable', () => {
    const { project, item } = place(base, 'servo_sg90')
    expect(verdict(project, `${item}:lead.gnd`, `${BOARD_ITEM}:usb.gnd`)).toMatchObject({ ok: false, why: expect.stringContaining('takes a cable') })
    const net = buildNetlist(hardwareOf(project), contextOf(project, a))
    expect(pinsNear(net, net.nodes.get(`${BOARD_ITEM}:usb.vbus`)!.at, { radius: 3 }).some((n) => n.key.startsWith(`${BOARD_ITEM}:usb.`))).toBe(false)
  })

  it('offers a PWM pin only where one can be: not an ADC input, not a reserved pin', () => {
    const { project, item } = place(base, 'servo_sg90')
    expect(verdict(project, `${item}:lead.sig`, `${BOARD_ITEM}:pwm3.pwm`).ok).toBe(true)
    expect(verdict(project, `${item}:lead.sig`, `${BOARD_ITEM}:gpio.io18`).ok).toBe(true)
    expect(verdict(project, `${item}:lead.sig`, `${BOARD_ITEM}:gpio.io46`)).toMatchObject({ ok: false })
    expect(verdict(project, `${item}:lead.sig`, `${BOARD_ITEM}:ain.ain0`)).toMatchObject({ ok: false })
    expect(verdict(project, `${item}:lead.vcc`, `${BOARD_ITEM}:pwr_out.vs`).ok).toBe(true)
    expect(verdict(project, `${item}:lead.gnd`, `${BOARD_ITEM}:pwm0.vcc`)).toMatchObject({ ok: false })
  })

  it('never joins two supplies', () => {
    expect(verdict(base, `${BOARD_ITEM}:pwm0.vcc`, `${BOARD_ITEM}:pwr_out.reg1`)).toMatchObject({ ok: false })
  })

  it('feeds a 3-6 V motor from Rail A when the input is 12 V, at the 5 V the servos on it take too', () => {
    let project: ProjectFile = { ...base, hardware: { ...hardwareOf(base), power: { source: 'src_ext', input_mV: 12000 } } }
    let servo: string
    ;({ project, item: servo } = place(project, 'servo_sg90'))
    project = wire(project, { item: servo, pin: 'lead.vcc' }, { item: BOARD_ITEM, pin: 'pwm0.vcc' })
    let motor: string
    ;({ project, item: motor } = place(project, 'motor_dc_tt'))
    project = wire(project, { item: motor, pin: 'leads.a' }, { item: BOARD_ITEM, pin: 'drv2.out1' })
    expect(verdict(project, `${motor}:leads.b`, `${BOARD_ITEM}:drv2.out3`)).toMatchObject({ ok: false })
    project = wire(project, { item: motor, pin: 'leads.b' }, { item: BOARD_ITEM, pin: 'drv2.out2' })
    const s = scene(project)
    expect(s.power.elements.get(`${BOARD_ITEM}:mux_drv2`)?.input).toBe(1)
    expect(s.power.elements.get(`${BOARD_ITEM}:reg_a`)?.setpoint_mV).toBe(5000)
    expect(project.boardDefaults.find((d) => d.device === 8)?.steps).toEqual([
      { kind: 'device', device: 8, contract: 'POWER_SUPPLY', command: 'SOURCE', args: { source: 'SYS_POWER_SUPPLY_B' } },
    ])
    expect(s.power.problems.filter((p) => p.severity === 'error')).toEqual([])
  })

  it('a breakout on the user I2C bus: its device goes on that bus, a servo on its channel takes its pin', () => {
    let { project, item: pca } = place(base, 'pca9685_breakout')
    const pcaId = project.devices.at(-1)!.id
    project = wire(project, { item: pca, pin: 'ctrl_in.sda' }, { item: BOARD_ITEM, pin: 'user_i2c.sda' })
    expect(project.devices.at(-1)!.config?.i2c_bus).toBe(1)
    let servo: string
    ;({ project, item: servo } = place(project, 'servo_mg996r'))
    project = wire(project, { item: servo, pin: 'lead.sig' }, { item: pca, pin: 'ch5.pwm' })
    expect(project.devices.find((d) => d.type === 'servo')!.config?.pwm_pin).toEqual({ device: pcaId, pin: 5 })
    // V+ only passes power on: the servo's 6 V comes from what feeds it.
    project = wire(project, { item: servo, pin: 'lead.vcc' }, { item: pca, pin: 'ch5.vplus' })
    project = wire(project, { item: pca, pin: 'vplus_in.vplus' }, { item: BOARD_ITEM, pin: 'pwr_out.reg1' })
    expect(project.boardDefaults.find((d) => d.device === 11)?.steps[0]).toMatchObject({ command: 'VOLTAGE', args: { voltage_mV: 5000 } })
  })

  it('sets a rail to what its sinks want when 5 V does not fit them all', () => {
    let { project, item: motor } = place(base, 'motor_dc_gear_12v')
    project = { ...project, hardware: { ...hardwareOf(project), power: { source: 'src_ext', input_mV: 24000 } } }
    project = wire(project, { item: motor, pin: 'leads.a' }, { item: BOARD_ITEM, pin: 'drv1.out1' })
    project = wire(project, { item: motor, pin: 'leads.b' }, { item: BOARD_ITEM, pin: 'drv1.out2' })
    const s = scene(project)
    expect(s.power.elements.get(`${BOARD_ITEM}:mux_drv1`)?.input).toBe(1)
    expect(s.power.elements.get(`${BOARD_ITEM}:reg_b`)?.setpoint_mV).toBe(12000)
  })

  it('takes the + of the XT60 on its chamfered pin', () => {
    const xt60 = library.get('runit_board_r1')!.connectors.find((c) => c.id === 'xt60')!
    expect(xt60.pins.map((p) => p.label)).toEqual(['GND', '+'])
    expect(connectorPinPoints(xt60)[1]!.x).toBeCloseTo(60.7)
  })

  it('reads a stored layout and leaves out what it cannot read', () => {
    const hw = readHardware({ items: [{ id: 'part1', part: 'servo_sg90', x: 1, y: 2, rotation: 45 }, { nope: 1 }], wires: [{ id: 'w', a: { item: 'part1', pin: 'lead.sig' }, b: { item: 'gone', pin: 'x' } }] })
    expect(hw.items).toEqual([{ id: 'part1', part: 'servo_sg90', x: 1, y: 2, rotation: 0 }])
    expect(hw.wires).toEqual([])
  })
})
