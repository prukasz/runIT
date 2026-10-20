import { describe, expect, it } from 'vitest'
import { bundledBoard } from '@backend'
import { defaultReadings, formatReading } from '../../src/app/power/powerDisplay'

describe('the power readings', () => {
  it('are short, in volts, amps and watts', () => {
    expect(formatReading(5012, 'mV')).toBe('5.01V')
    expect(formatReading(12034, 'mV')).toBe('12.0V')
    expect(formatReading(850, 'mA')).toBe('0.85A')
    expect(formatReading(10230, 'mW')).toBe('10.2W')
    expect(formatReading(80, '%')).toBe('80%')
  })

  it('come from board.json: the input and each rail by a power monitor channel', () => {
    const power = bundledBoard().power
    expect(power.input).toMatchObject({ device: 12, channel: 1 })
    expect(power.rails.map((r) => [r.name, r.regulator, r.monitor?.channel])).toEqual([['Rail A', 10, 2], ['Rail B', 11, 0]])
  })

  it('by default: the input in short (source, voltage, power), each rail with More (voltage, current)', () => {
    const readings = defaultReadings(bundledBoard())
    expect(readings.filter((r) => r.main).map((r) => [r.read.command, r.field])).toEqual([['STATUS', 'source'], ['STATUS', 'input_mV'], ['POWER', undefined]])
    expect(readings.filter((r) => !r.main).map((r) => [r.name, r.read.command])).toEqual([['Rail A', 'VOLTAGE'], ['Rail A', 'CURRENT'], ['Rail B', 'VOLTAGE'], ['Rail B', 'CURRENT']])
    expect(readings.find((r) => r.name === 'Rail B')).toMatchObject({ read: { kind: 'device', device: 12, contract: 'POWER_MONITOR', command: 'VOLTAGE' }, args: { channel: 0 } })
  })
})

describe('what a reading can read', () => {
  it('only what is about power: the power manager, and the power contracts and own reads of the devices that have one', async () => {
    const { bundledServiceCatalog } = await import('@backend')
    const { readCommands } = await import('../../src/app/power/powerDisplay')
    const { deviceList } = await import('../../src/app/devices/devices')
    const { emptyProject } = await import('../../src/app/project/projectFile')
    const catalog = bundledServiceCatalog()
    const commands = readCommands(catalog, deviceList(emptyProject(), catalog, bundledBoard()))
    expect(new Set(commands.filter((c) => c.read.kind === 'service').map((c) => (c.read.kind === 'service' ? c.read.service : '')))).toEqual(new Set(['POWER_MANAGER']))
    expect(new Set(commands.map((c) => c.group))).toEqual(new Set(['Power manager', 'Motor 1 supply', 'Motor 2 supply', 'Rail A', 'Rail B', 'Power monitor', 'USB-C PD', 'Motor driver 1', 'Motor driver 2']))
  })
})

describe('readings with the OPS answers', () => {
  it('only what a device answers: a DRV8962 reads its current, and nothing without its IPROPI pins', async () => {
    const { bundledServiceCatalog } = await import('@backend')
    const { readCommands } = await import('../../src/app/power/powerDisplay')
    const { deviceList } = await import('../../src/app/devices/devices')
    const { deviceOp } = await import('@backend')
    const { emptyProject } = await import('../../src/app/project/projectFile')
    const catalog = bundledServiceCatalog()
    const devices = deviceList(emptyProject(), catalog, bundledBoard())
    const monitor = catalog.contracts.get('POWER_MONITOR')!
    const current = deviceOp(catalog, monitor, monitor.commands.get('CURRENT')!)
    const ops = new Map([[5, new Map([[current, 1]])], [6, new Map<number, number>()]])
    const commands = readCommands(catalog, devices, (id) => ops.get(id))
    expect(commands.filter((c) => c.group === 'Motor driver 1').map((c) => c.label)).toEqual(['Power monitor: Current'])
    expect(commands.filter((c) => c.group === 'Motor driver 2')).toEqual([])
  })
})

describe('a devkit board (board_devkit.json)', () => {
  it('has the ESP pins only: no chips, no power layout, the input read from the power manager alone', async () => {
    const { bundledServiceCatalog } = await import('@backend')
    const { deviceList } = await import('../../src/app/devices/devices')
    const { emptyProject } = await import('../../src/app/project/projectFile')
    const board = bundledBoard('devkit')
    expect(board.devices.map((d) => [d.id, d.descriptor])).toEqual([[0, 'device_gpio_esp']])
    expect(board.pinSetup).toEqual([])
    expect(board.power).toEqual({ rails: [] })
    expect(defaultReadings(board).map((r) => [r.read.command, r.field])).toEqual([['STATUS', 'source'], ['STATUS', 'input_mV']])
    expect(deviceList(emptyProject(), bundledServiceCatalog(), board).map((d) => d.id)).toEqual([0])
  })

  it('is not the default: the app is the PCB\'s unless it was started for a devkit', () => {
    expect(bundledBoard().name).toBe('runIT')
    expect(bundledBoard('devkit').name).toBe('runIT devkit')
  })
})
