import { describe, expect, it } from 'vitest'
import { boardTerminals, bindingSpec, CONNECTOR_TYPES, connectorBounds, connectorType, inConnector, newBoard, nextConnectorId, onBoard, pinAt, pinPosition, pointInConnector, renderBoardSvg, terminalId, validateBoard } from '../../../src/domain/board'
import type { BoardConnector, BoardTemplate, SpecContext } from '../../../src/domain/board'
import type { PinSpec } from '../../../src/domain/hardware'

const connector = (patch: Partial<BoardConnector> = {}): BoardConnector => ({
  id: 'c1',
  type: 'header1x4',
  x: 20,
  y: 20,
  rotation: 0,
  mirror: false,
  pins: Array.from({ length: 4 }, () => ({ bind: { type: 'none' as const } })),
  ...patch,
})

const context: SpecContext = {
  signals: [{ id: 'i2c-sda', label: 'I2C SDA', dir: 'inout' }, { id: 'i2c-scl', label: 'I2C SCL', dir: 'out' }, { id: 'uart-tx', label: 'UART TX', dir: 'out' }],
  nets: { GND: { kind: 'gnd', label: 'Ground' }, '3V3': { kind: 'supply', label: '3.3 V', mV_min: 3300, mV_max: 3300 }, VS: { kind: 'supply', label: 'Board input', note: '6 to 20 V' } },
  sysPin: ({ device, pin }): PinSpec | undefined => (device === 0 && pin < 48 ? { kind: 'io', dir: 'inout', label: `GPIO${pin}`, modes: ['BINARY', 'PWM'], mV_max: 3600 } : undefined),
}

const close = (a: { x: number; y: number }, x: number, y: number) => {
  expect(a.x).toBeCloseTo(x, 6)
  expect(a.y).toBeCloseTo(y, 6)
}

describe('connector library', () => {
  it('every type has pins inside its body and a draw that names a terminal per pin', () => {
    expect(CONNECTOR_TYPES.length).toBeGreaterThanOrEqual(10)
    for (const type of CONNECTOR_TYPES) {
      expect(type.pins.length).toBeGreaterThan(0)
      for (const pin of type.pins) {
        expect(Math.abs(pin.x)).toBeLessThanOrEqual(type.width / 2)
        expect(Math.abs(pin.y)).toBeLessThanOrEqual(type.height / 2)
      }
      const seen: number[] = []
      type.draw((index, shape) => {
        seen.push(index)
        return shape
      })
      expect(seen.sort()).toEqual(type.pins.map((_, index) => index))
    }
  })

  it('knows the parts the concept calls for: screw terminals, gold pins, XT60, USB-C', () => {
    for (const id of ['screw2', 'header1x4', 'xt60', 'usbc']) expect(connectorType(id)).toBeDefined()
    expect(connectorType('screw3')?.pins).toHaveLength(3)
    expect(connectorType('header2x4')?.pins).toHaveLength(8)
  })
})

describe('geometry', () => {
  it('moves a local point by the connector position; rotation is a quarter turn clockwise', () => {
    close(onBoard(connector(), { x: 2, y: 1 }), 22, 21)
    close(onBoard(connector({ rotation: 90 }), { x: 2, y: 0 }), 20, 22)
    close(onBoard(connector({ rotation: 180 }), { x: 2, y: 1 }), 18, 19)
    close(onBoard(connector({ rotation: 270 }), { x: 2, y: 0 }), 20, 18)
  })

  it('mirrors left-right before rotating', () => {
    close(onBoard(connector({ mirror: true }), { x: 2, y: 1 }), 18, 21)
    close(onBoard(connector({ mirror: true, rotation: 90 }), { x: 2, y: 0 }), 20, 18)
  })

  it('inConnector is the inverse of onBoard for every rotation and mirror', () => {
    for (const rotation of [0, 90, 180, 270] as const) {
      for (const mirror of [false, true]) {
        const c = connector({ rotation, mirror, x: 33.5, y: 12 })
        const back = inConnector(c, onBoard(c, { x: 3.2, y: -1.7 }))
        close(back, 3.2, -1.7)
      }
    }
  })

  it('bounds follow the rotation; a point is inside the body, a pin is found within reach', () => {
    const flat = connectorBounds(connector())
    expect(flat.right - flat.left).toBeCloseTo(4 * 2.54, 6)
    expect(flat.bottom - flat.top).toBeCloseTo(2.54, 6)
    const turned = connectorBounds(connector({ rotation: 90 }))
    expect(turned.right - turned.left).toBeCloseTo(2.54, 6)
    expect(turned.bottom - turned.top).toBeCloseTo(4 * 2.54, 6)
    expect(pointInConnector(connector(), { x: 20, y: 20 })).toBe(true)
    expect(pointInConnector(connector(), { x: 30, y: 20 })).toBe(false)
    const second = pinPosition(connector(), 1)!
    expect(pinAt(connector(), { x: second.x + 0.2, y: second.y }, 1)).toBe(1)
    expect(pinAt(connector(), { x: 0, y: 0 }, 1)).toBeUndefined()
  })
})

describe('art', () => {
  const board: BoardTemplate = { ...newBoard('b', 'B'), connectors: [connector(), connector({ id: 'c2', type: 'xt60', x: 50, y: 30, rotation: 90, mirror: true, pins: [{ label: 'V+', bind: { type: 'none' } }, { bind: { type: 'none' } }] })] }
  const svg = renderBoardSvg(board)

  it('is a viewBox in art units with the outline, the holes and a terminal group per connector pin', () => {
    expect(svg).toContain(`viewBox="0 0 ${80 * 20} ${60 * 20}"`)
    expect(svg).toContain('class="pcb"')
    expect(svg.match(/class="hole-bore"/g)).toHaveLength(4)
    for (const id of ['c1-1', 'c1-2', 'c1-3', 'c1-4', 'c2-1', 'c2-2']) expect(svg).toContain(`<g id="${id}">`)
    expect(terminalId('c1', 3)).toBe('c1-3')
  })

  it('the connector wrapper has no id, so the pin group is the terminal', () => {
    expect(svg.match(/<g data-connector="c1"[^>]*>/)?.[0]).not.toContain(' id=')
  })

  it('puts the connector transform on the wrapper and keeps the labels upright', () => {
    expect(svg).toContain('translate(50 30) rotate(90) scale(-1 1)')
    expect(svg).toContain('rotate(-90)')
    expect(svg).toContain('>V+</text>')
  })

  it('escapes names so a label cannot break the markup', () => {
    const odd: BoardTemplate = { ...newBoard('b', 'B'), connectors: [connector({ pins: [{ label: '<b>&"', bind: { type: 'none' } }, ...connector().pins.slice(1)] })] }
    expect(renderBoardSvg(odd)).not.toContain('<b>&"')
  })

  it('every rule of the style is on its own line starting with a dot, so the app can scope it', () => {
    const css = /<style>([\s\S]*?)<\/style>/.exec(svg)![1].trim().split('\n')
    expect(css.every((line) => line.startsWith('.'))).toBe(true)
  })
})

describe('bindings', () => {
  it('a ground net is ground; a power net is a source, or an input when the pin takes it', () => {
    expect(bindingSpec({ type: 'net', net: 'GND' }, 'GND', context)).toMatchObject({ kind: 'gnd', net: 'GND' })
    expect(bindingSpec({ type: 'net', net: '3V3' }, '3V3', context)).toMatchObject({ kind: 'supply_out', dir: 'out', net: '3V3' })
    expect(bindingSpec({ type: 'net', net: 'VS', role: 'input' }, 'V+', context)).toMatchObject({ kind: 'supply_in', dir: 'in', net: 'VS' })
    expect(bindingSpec({ type: 'net', net: 'NOPE' }, 'x', context)).toBeUndefined()
  })

  it('a bus signal has its direction and may carry the sys pin behind it', () => {
    expect(bindingSpec({ type: 'signal', signal: 'i2c-sda', sys: { device: 0, pin: 40 } }, 'SDA', context)).toMatchObject({ kind: 'bus', dir: 'inout', signal: 'i2c-sda', sysPin: { device: 0, pin: 40 } })
    expect(bindingSpec({ type: 'signal', signal: 'uart-tx' }, 'TX', context)).toMatchObject({ kind: 'bus', dir: 'out', signal: 'uart-tx' })
    expect(bindingSpec({ type: 'signal', signal: 'nope' }, 'x', context)).toBeUndefined()
  })

  it('a sys pin takes what its provider gives it, with the board\'s own limits on top', () => {
    expect(bindingSpec({ type: 'sys', device: 0, pin: 46 }, 'IO46', context)).toMatchObject({ kind: 'io', dir: 'inout', label: 'IO46', modes: ['BINARY', 'PWM'], sysPin: { device: 0, pin: 46 } })
    expect(bindingSpec({ type: 'sys', device: 0, pin: 3, overrides: { dir: 'in', modes: ['ADC'], mV_max: 20000 } }, 'AIN0', context)).toMatchObject({ dir: 'in', modes: ['ADC'], mV_max: 20000 })
    expect(bindingSpec({ type: 'sys', device: 9, pin: 1 }, 'x', context)).toBeUndefined()
    expect(bindingSpec({ type: 'none' }, 'x', context)).toBeUndefined()
  })

  it('a board lists only its bound pins, by terminal ID, labelled as the board labels them', () => {
    const board: BoardTemplate = {
      ...newBoard('b', 'B'),
      connectors: [connector({ pins: [{ label: '3V3', bind: { type: 'net', net: '3V3' } }, { bind: { type: 'net', net: 'GND' } }, { label: 'IO46', bind: { type: 'sys', device: 0, pin: 46 } }, { bind: { type: 'none' } }] })],
    }
    const terminals = boardTerminals(board, context)
    expect([...terminals.keys()]).toEqual(['c1-1', 'c1-2', 'c1-3'])
    expect(terminals.get('c1-1')?.label).toBe('3V3')
    expect(terminals.get('c1-2')?.label).toBe('2')
    expect(terminals.get('c1-3')?.sysPin).toEqual({ device: 0, pin: 46 })
  })
})

describe('checks', () => {
  it('a new board has none; the next connector ID is free', () => {
    expect(validateBoard(newBoard('b', 'B'))).toEqual([])
    expect(nextConnectorId({ ...newBoard('b', 'B'), connectors: [connector(), connector({ id: 'c3' })] })).toBe('c2')
  })

  it('refuses a bad board ID, an unknown type, a wrong pin count and a repeated connector ID', () => {
    const board = { ...newBoard('Bad id', 'B'), connectors: [connector({ type: 'nope' }), connector({ id: 'c2', pins: [] }), connector({ id: 'c2', x: 60 })] }
    const messages = validateBoard(board).filter((issue) => issue.severity === 'error').map((issue) => issue.message).join(' | ')
    expect(messages).toContain('board ID')
    expect(messages).toContain('does not exist')
    expect(messages).toContain('has 4 pins')
    expect(messages).toContain('used twice')
  })

  it('warns about a connector off the board, overlapping connectors and a sys pin on two pins', () => {
    const sys = { type: 'sys' as const, device: 0, pin: 5 }
    const board = { ...newBoard('b', 'B'), connectors: [connector({ x: 1 }), connector({ id: 'c2', x: 5, pins: [{ bind: sys }, { bind: sys }, { bind: { type: 'none' } }, { bind: { type: 'none' } }] })] }
    const warnings = validateBoard(board).filter((issue) => issue.severity === 'warning').map((issue) => issue.message).join(' | ')
    expect(warnings).toContain('outside the board')
    expect(warnings).toContain('overlap')
    expect(warnings).toContain('Sys pin 0.5')
  })
})

describe('placing a new connector', () => {
  it('goes to the middle of an empty board', async () => {
    const { freeSpot } = await import('../../../src/domain/board')
    const spot = freeSpot(newBoard('b', 'B'), connectorType('header1x4')!)
    expect(spot.x).toBeCloseTo(40, 0)
    expect(spot.y).toBeCloseTo(30, 0)
  })

  it('goes to the nearest free spot when the middle is taken, inside the board and clear of the others', async () => {
    const { freeSpot } = await import('../../../src/domain/board')
    const type = connectorType('header1x4')!
    const board: BoardTemplate = { ...newBoard('b', 'B'), connectors: [connector({ x: 40, y: 30 })] }
    const spot = freeSpot(board, type)
    const placed = connector({ id: 'c2', x: spot.x, y: spot.y })
    const a = connectorBounds(board.connectors[0])
    const b2 = connectorBounds(placed)
    expect(b2.left < a.right && a.left < b2.right && b2.top < a.bottom && a.top < b2.bottom).toBe(false)
    expect(validateBoard({ ...board, connectors: [...board.connectors, placed] }).filter((issue) => issue.severity === 'warning')).toEqual([])
  })

  it('falls back to the middle on a board with no room', async () => {
    const { freeSpot } = await import('../../../src/domain/board')
    const tiny: BoardTemplate = { ...newBoard('b', 'B'), width: 12, height: 12 }
    expect(freeSpot(tiny, connectorType('screw4')!)).toEqual({ x: 6, y: 6 })
  })
})
