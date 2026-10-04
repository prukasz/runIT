import { describe, expect, it } from 'vitest'
import { buildNets, checkWiring, configLinks, connectionProblem, netContext, netVoltage } from '../../../src/domain/hardware'
import type { Endpoint, NetSpec, PinSpec, Wire, WiringContext, WiringDevice } from '../../../src/domain/hardware'

const nets: Record<string, NetSpec> = {
  GND: { kind: 'gnd', label: 'Ground' },
  '3V3': { kind: 'supply', label: '3.3 V', mV_min: 3300, mV_max: 3300 },
  REG2: { kind: 'supply', label: 'REG2' },
  VS: { kind: 'supply', label: 'Board input', mV_min: 6000, mV_max: 20000 },
}
const pin = (spec: Partial<PinSpec> & Pick<PinSpec, 'kind' | 'label'>): PinSpec => ({ dir: 'in', modes: [], ...spec })

const board = new Map<string, PinSpec>([
  ['gnd-a', pin({ kind: 'gnd', label: 'GND', net: 'GND' })],
  ['gnd-b', pin({ kind: 'gnd', label: 'GND', net: 'GND' })],
  ['3v3-a', pin({ kind: 'supply_out', dir: 'out', label: '3V3', net: '3V3' })],
  ['3v3-b', pin({ kind: 'supply_out', dir: 'out', label: '3V3', net: '3V3' })],
  ['reg2', pin({ kind: 'supply_out', dir: 'out', label: 'REG2', net: 'REG2' })],
  ['vs', pin({ kind: 'supply_out', dir: 'out', label: 'VS', net: 'VS' })],
  ['io46', pin({ kind: 'io', dir: 'inout', label: 'IO46', modes: ['BINARY', 'PWM'], mV_max: 3600, sysPin: { device: 0, pin: 46 } })],
  ['sda', pin({ kind: 'bus', dir: 'inout', label: 'SDA', signal: 'i2c-sda' })],
  ['scl', pin({ kind: 'bus', dir: 'out', label: 'SCL', signal: 'i2c-scl' })],
  ['tx', pin({ kind: 'bus', dir: 'out', label: 'TX', signal: 'uart-tx' })],
  ['ain0', pin({ kind: 'io', dir: 'in', label: 'AIN0', modes: ['ADC'], mV_max: 20000 })],
])

const chip: WiringDevice = {
  ref: 'chip',
  name: 'chip',
  pins: new Map<string, PinSpec>([
    ['VDD', pin({ kind: 'supply_in', label: 'VDD', mV_min: 2300, mV_max: 5500 })],
    ['VSS', pin({ kind: 'gnd', label: 'VSS' })],
    ['SDA', pin({ kind: 'bus', dir: 'inout', label: 'SDA', signal: 'i2c-sda' })],
    ['SCL', pin({ kind: 'bus', dir: 'in', label: 'SCL', signal: 'i2c-scl' })],
    ['OE', pin({ kind: 'io', dir: 'in', label: 'OE', modes: ['BINARY'], mV_max: 5500, cfgField: 'oe_pin' })],
    ['RX', pin({ kind: 'bus', dir: 'in', label: 'RX', signal: 'uart-rx' })],
  ]),
}
const other: WiringDevice = { ref: 'other', name: 'other', pins: new Map<string, PinSpec>([['VDD', pin({ kind: 'supply_in', label: 'VDD', mV_max: 3600 })], ['OUT', pin({ kind: 'io', dir: 'out', label: 'OUT' })], ['OUT2', pin({ kind: 'io', dir: 'out', label: 'OUT2' })], ['IN', pin({ kind: 'io', dir: 'in', label: 'IN' })]]) }
const context: WiringContext = { nets, board, devices: [chip, other] }

const b = (pinName: string): Endpoint => ({ device: 'board', pin: pinName })
const d = (device: string, pinName: string): Endpoint => ({ device, pin: pinName })
let counter = 0
const wire = (a: Endpoint, z: Endpoint): Wire => ({ id: `w${++counter}`, a, b: z })
const codes = (wires: Wire[]) => checkWiring(wires, context).map((issue) => issue.code)

describe('connection rules', () => {
  const spec = (name: string): PinSpec => (board.get(name) ?? chip.pins.get(name) ?? other.pins.get(name)) as PinSpec

  it('ground goes to ground, a supply to a supply, a bus to its own signal', () => {
    expect(connectionProblem(spec('gnd-a'), chip.pins.get('VSS') as PinSpec)).toBeUndefined()
    expect(connectionProblem(spec('gnd-a'), chip.pins.get('VDD') as PinSpec)).toContain('Ground')
    expect(connectionProblem(spec('3v3-a'), chip.pins.get('VDD') as PinSpec)).toBeUndefined()
    expect(connectionProblem(spec('3v3-a'), chip.pins.get('OE') as PinSpec)).toContain('supply')
    expect(connectionProblem(spec('sda'), chip.pins.get('SDA') as PinSpec)).toBeUndefined()
    expect(connectionProblem(spec('sda'), chip.pins.get('SCL') as PinSpec)).toContain('not the same bus signal')
    expect(connectionProblem(spec('sda'), chip.pins.get('OE') as PinSpec)).toContain('bus')
  })

  it('a UART transmit meets a receive, not another transmit', () => {
    expect(connectionProblem(spec('tx'), chip.pins.get('RX') as PinSpec)).toBeUndefined()
    expect(connectionProblem(spec('tx'), spec('tx'))).toBeDefined()
  })

  it('a producer feeds a consumer; a pin that can be either takes the role the other end needs', () => {
    expect(connectionProblem(spec('io46'), chip.pins.get('OE') as PinSpec)).toBeUndefined()
    expect(connectionProblem(other.pins.get('OUT') as PinSpec, spec('io46'))).toBeUndefined()
    expect(connectionProblem(other.pins.get('OUT') as PinSpec, chip.pins.get('OE') as PinSpec)).toBeUndefined()
  })

  it('a GPIO (either) may feed an analog input, whatever their modes: the consumer reads the level', () => {
    expect(connectionProblem(spec('io46'), spec('ain0'))).toBeUndefined()
    expect(connectionProblem(spec('ain0'), spec('io46'))).toBeUndefined()
    // A PWM-capable producer into an ADC consumer: no shared mode is needed.
    expect(connectionProblem(pin({ kind: 'io', dir: 'out', label: 'PWM0', modes: ['PWM'] }), spec('ain0'))).toBeUndefined()
  })

  it('two producers fight', () => {
    expect(connectionProblem(other.pins.get('OUT') as PinSpec, pin({ kind: 'io', dir: 'out', label: 'X' }))).toContain('both producers')
  })

  it('two consumers may be wired (a producer can join the net), but the net reports no producer', () => {
    expect(connectionProblem(spec('ain0'), chip.pins.get('OE') as PinSpec)).toBeUndefined()
    expect(codes([wire(d('chip', 'OE'), b('ain0'))])).toContain('no-driver')
  })

  it('two supply outputs short unless they are the same named net', () => {
    expect(connectionProblem(spec('3v3-a'), spec('reg2'))).toContain('short')
    expect(connectionProblem(spec('3v3-a'), spec('3v3-b'))).toBeUndefined()
  })
})

describe('nets', () => {
  it('joins every terminal of a named board net', () => {
    const found = buildNets([wire(d('chip', 'VSS'), b('gnd-a'))], netContext(context))
    expect(found).toHaveLength(1)
    expect(found[0].role).toBe('ground')
    expect(found[0].endpoints.map((e) => e.pin).sort()).toEqual(['VSS', 'gnd-a', 'gnd-b'])
  })

  it('a wire to any terminal of a net reaches the others, so two devices on one rail are one net', () => {
    const found = buildNets([wire(d('chip', 'VDD'), b('3v3-a')), wire(d('other', 'VDD'), b('3v3-b'))], netContext(context))
    expect(found).toHaveLength(1)
    expect(found[0].named).toEqual(['3V3'])
    expect(netVoltage(found[0], nets)).toEqual({ min: 3300, max: 3300 })
  })

  it('keeps separate nets separate and reports a runtime-set rail as unknown', () => {
    const found = buildNets([wire(d('chip', 'VDD'), b('reg2')), wire(d('chip', 'VSS'), b('gnd-a'))], netContext(context))
    expect(found.map((net) => net.role).sort()).toEqual(['ground', 'power'])
    expect(netVoltage(found.find((net) => net.role === 'power') as never, nets)).toBeUndefined()
  })
})

describe('wiring checks', () => {
  it('a clean design has no errors', () => {
    const wires = [wire(d('chip', 'VDD'), b('3v3-a')), wire(d('chip', 'VSS'), b('gnd-a')), wire(d('chip', 'SDA'), b('sda')), wire(d('chip', 'SCL'), b('scl'))]
    expect(checkWiring(wires, context).filter((issue) => issue.severity === 'error')).toEqual([])
  })

  it('reports an incompatible wire, a wire to a missing pin, a self wire and a duplicate', () => {
    expect(codes([wire(d('chip', 'VDD'), b('gnd-a'))])).toContain('incompatible')
    expect(codes([wire(d('chip', 'NOPE'), b('gnd-a'))])).toContain('dangling')
    expect(codes([wire(d('ghost', 'VDD'), b('3v3-a'))])).toContain('dangling')
    expect(codes([wire(d('chip', 'VSS'), d('chip', 'VSS'))])).toContain('self')
    expect(codes([wire(d('chip', 'VSS'), b('gnd-a')), wire(b('gnd-a'), d('chip', 'VSS'))])).toContain('duplicate')
  })

  it('reports a short between two supplies and between a supply and ground', () => {
    expect(codes([wire(d('chip', 'VDD'), b('3v3-a')), wire(d('chip', 'VDD'), b('reg2'))])).toContain('short')
  })

  it('reports overvoltage: VS (up to 20 V) on a 5.5 V pin, and 3V3 on a pin that needs more', () => {
    const over = checkWiring([wire(d('chip', 'VDD'), b('vs'))], context).find((issue) => issue.code === 'overvoltage')
    expect(over?.severity).toBe('error')
    expect(over?.message).toContain('5.5 V')
    expect(codes([wire(d('chip', 'VDD'), b('3v3-a'))])).not.toContain('overvoltage')
    expect(codes([wire(d('other', 'VDD'), b('vs'))])).toContain('overvoltage')
  })

  it('a runtime-set rail is a note about the setting, not an error', () => {
    const issues = checkWiring([wire(d('chip', 'VDD'), b('reg2')), wire(d('chip', 'VSS'), b('gnd-a'))], context)
    expect(issues.find((issue) => issue.code === 'regulator-setting')?.severity).toBe('info')
    expect(issues.filter((issue) => issue.severity === 'error')).toEqual([])
  })

  it('reports a device that is wired but not powered or not grounded', () => {
    expect(codes([wire(d('chip', 'SDA'), b('sda')), wire(d('chip', 'SCL'), b('scl'))])).toEqual(expect.arrayContaining(['unpowered', 'no-ground']))
    expect(codes([wire(d('chip', 'VDD'), b('3v3-a'))])).toContain('no-ground')
  })

  it('reports I2C with only one of its two wires', () => {
    const issue = checkWiring([wire(d('chip', 'VDD'), b('3v3-a')), wire(d('chip', 'VSS'), b('gnd-a')), wire(d('chip', 'SDA'), b('sda'))], context).find((entry) => entry.code === 'i2c-incomplete')
    expect(issue?.pin).toBe('SCL')
  })

  it('reports two producers driving one net', () => {
    const issue = checkWiring([wire(d('other', 'OUT'), d('chip', 'OE')), wire(d('other', 'OUT2'), d('chip', 'OE'))], context).find((entry) => entry.code === 'two-drivers')
    expect(issue?.severity).toBe('error')
  })

  it('reports a consumer nothing produces for, and is quiet once a producer is on the net', () => {
    expect(codes([wire(d('chip', 'OE'), d('other', 'IN'))])).toContain('no-driver')
    expect(codes([wire(d('chip', 'OE'), d('other', 'IN')), wire(d('other', 'OUT'), d('chip', 'OE'))])).not.toContain('no-driver')
  })

  it('a pin that can be either, alone as the producer, has to be set as an output', () => {
    const issue = checkWiring([wire(b('io46'), b('ain0'))], context).find((entry) => entry.code === 'set-output')
    expect(issue).toMatchObject({ severity: 'info', device: 'board', pin: 'io46' })
    expect(issue?.message).toContain('set it as an output')
    expect(codes([wire(b('io46'), b('ain0'))])).not.toContain('no-driver')
  })

  it('a pin that can be either, listening to a producer, has to be set as an input', () => {
    const issue = checkWiring([wire(d('other', 'OUT'), b('io46'))], context).find((entry) => entry.code === 'set-input')
    expect(issue).toMatchObject({ severity: 'info', device: 'board', pin: 'io46' })
  })

  it('an unwired device raises nothing', () => {
    expect(checkWiring([], context)).toEqual([])
  })

  it('orders errors before warnings', () => {
    const issues = checkWiring([wire(d('chip', 'SDA'), b('sda')), wire(d('chip', 'VDD'), b('vs'))], context)
    const order = issues.map((issue) => issue.severity)
    expect(order).toEqual([...order].sort((x, y) => ['error', 'warning', 'info'].indexOf(x) - ['error', 'warning', 'info'].indexOf(y)))
  })
})

describe('config links', () => {
  it('a device pin with a cfg field wired to a board GPIO implies its install pin ref', () => {
    const link = configLinks([wire(d('chip', 'OE'), b('io46'))], context)
    expect(link).toHaveLength(1)
    expect(link[0]).toMatchObject({ device: 'chip', field: 'oe_pin', sysPin: { device: 0, pin: 46 }, terminal: 'io46', pin: 'OE' })
  })

  it('other pins and other board terminals imply nothing', () => {
    expect(configLinks([wire(d('chip', 'VSS'), b('gnd-a')), wire(d('chip', 'OE'), d('other', 'OUT'))], context)).toEqual([])
  })
})
