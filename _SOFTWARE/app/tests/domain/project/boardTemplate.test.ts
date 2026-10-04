import { describe, expect, it } from 'vitest'
import { newBoard } from '../../../src/domain/board'
import type { BoardTemplate } from '../../../src/domain/board'
import { createProject, parseBoardTemplate, parseProject, serializeProject } from '../../../src/domain/project'

const board: BoardTemplate = {
  ...newBoard('lab', 'Lab board'),
  connectors: [
    {
      id: 'c1',
      type: 'header1x4',
      x: 20.5,
      y: 12,
      rotation: 90,
      mirror: true,
      label: 'Sensors',
      pins: [
        { label: '3V3', bind: { type: 'net', net: '3V3' } },
        { bind: { type: 'net', net: 'VS', role: 'input' } },
        { label: 'SDA', bind: { type: 'signal', signal: 'i2c-sda', sys: { device: 0, pin: 40 } } },
        { label: 'AIN0', bind: { type: 'sys', device: 2, pin: 0, overrides: { dir: 'in', modes: ['ADC'], mV_max: 20000 } } },
      ],
    },
    { id: 'c2', type: 'xt60', x: 60, y: 30, rotation: 0, mirror: false, pins: [{ bind: { type: 'none' } }, { bind: { type: 'sys', device: 0, pin: 46 } }] },
  ],
}

describe('board template in the project file', () => {
  it('round-trips with every kind of binding', () => {
    expect(parseProject(serializeProject({ ...createProject('t'), board })).board).toEqual(board)
  })

  it('is left out of the file when the project uses the built-in board', () => {
    expect(serializeProject(createProject('t'))).not.toContain('"board"')
    expect(parseProject(serializeProject(createProject('t'))).board).toBeUndefined()
  })

  it('keeps a fixed key order so saved files diff cleanly', () => {
    const text = serializeProject({ ...createProject('t'), board })
    expect(text.indexOf('"id": "lab"')).toBeLessThan(text.indexOf('"width"'))
    expect(text.indexOf('"width"')).toBeLessThan(text.indexOf('"connectors"'))
  })

  it('refuses a rotation that is not a quarter turn, a repeated connector ID and a size out of range', () => {
    const json = JSON.parse(JSON.stringify(board))
    expect(() => parseBoardTemplate({ ...json, connectors: [{ ...json.connectors[0], rotation: 45 }] }, 'board')).toThrow(/rotation/)
    expect(() => parseBoardTemplate({ ...json, connectors: [json.connectors[1], json.connectors[1]] }, 'board')).toThrow(/used twice/)
    expect(() => parseBoardTemplate({ ...json, width: 5 }, 'board')).toThrow(/from 10 to 400/)
    expect(() => parseBoardTemplate({ ...json, width: 800 }, 'board')).toThrow(/from 10 to 400/)
  })

  it('refuses a binding of an unknown type or a role that does not exist', () => {
    const json = JSON.parse(JSON.stringify(board))
    const withPin = (bind: unknown) => ({ ...json, connectors: [{ ...json.connectors[1], pins: [{ bind }, { bind: { type: 'none' } }] }] })
    expect(() => parseBoardTemplate(withPin({ type: 'magic' }), 'board')).toThrow(/expected one of/)
    expect(() => parseBoardTemplate(withPin({ type: 'net', net: 'GND', role: 'both' }), 'board')).toThrow(/role/)
    expect(() => parseBoardTemplate(withPin({ type: 'sys', device: 0 }), 'board')).toThrow(/pin/)
  })
})
