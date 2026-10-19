import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, bundledServiceCatalog } from '@backend'
import { openBoard } from './harness'
import type { Board } from './harness'

/* The onboard devices and their ids (runit-esp/components/app/runit, RUNIT.MD "Boot"). */
const ONBOARD: Record<number, string> = { 0: 'gpio_esp', 1: 'tca6424a', 2: 'ads7128', 3: 'pca9685', 4: 'dac53202', 5: 'drv8962', 6: 'drv8962', 7: 'power_mux', 8: 'power_mux', 10: 'tps55289', 11: 'tps55289', 12: 'ina3221', 13: 'ap33772s' }
const LED = 23 // TCA6424A pin 23: the status LED, lit once the boot got far enough

const catalog = bundledServiceCatalog()
let board: Board
beforeAll(async () => {
  board = await openBoard()
})
afterAll(async () => {
  await board.close()
})

const refused = async (promise: Promise<unknown>): Promise<CommandError> => {
  const error = await promise.then(
    () => undefined,
    (e: unknown) => e,
  )
  expect(error).toBeInstanceOf(CommandError)
  return error as CommandError
}

describe('the catalog has no problem and the services it names are on the board', () => {
  it('no problems in the bundle', () => {
    expect(catalog.problems).toEqual([])
  })

  it.each([
    ['SYS', 'PING'],
    ['SYS', 'DEADMAN_GET'],
    ['BLE', 'STATUS'],
    ['BLE', 'LIST'],
    ['CONNECTOR', 'INFO'],
    ['I2C', 'INFO'],
    ['LOG', 'GET'],
    ['POWER_MANAGER', 'GET_CONFIG'],
    ['POWER_MANAGER', 'GET_STATUS'],
    ['PROJECT', 'INFO'],
  ])('%s %s answers and decodes by its descriptor', async (service, command) => {
    const answer = await board.services.call(service!, command!)
    const returns = catalog.service(service!).commands.get(command!)!.returns
    if (returns) expect(answer.value, `${service} ${command} returned ${returns.kind}`).toBeDefined()
  })

  it('the power manager status is the 40 bytes of its struct, with a plausible input', async () => {
    const { value, data } = await board.services.call('POWER_MANAGER', 'GET_STATUS')
    expect(data.length).toBe(40)
    expect((value as { input_mV: number }).input_mV).toBeGreaterThan(3000)
  })
})

describe('devices', () => {
  it.each(Object.entries(ONBOARD))('device %s is a %s', async (id, type) => {
    const { value } = await board.services.device(Number(id), 'DEVICE', 'INFO')
    const typeId = (value as { type_id: number }).type_id
    expect(catalog.byTypeId.get(typeId)?.id, `type ${typeId}`).toBe(type)
  })

  it('a device that is not there: ERR_DEV_NOT_FOUND', async () => {
    const error = await refused(board.services.device(9, 'DEVICE', 'INFO'))
    expect(error.record!.cause!.info?.name).toBe('ERR_DEV_NOT_FOUND')
  })

  it('OPS of a device lists its commands', async () => {
    const { data } = await board.services.device(1, 'DEVICE', 'OPS')
    expect(data.length).toBeGreaterThan(0)
  })

  it('the status LED is lit; TOGGLE puts it out and back on, read back each time', async () => {
    const level = async () => (await board.services.device(1, 'IO', 'GET_LEVEL', { pin: LED })).value
    expect(String(await level())).toMatch(/High|1|true/i)
    await board.services.device(1, 'IO', 'TOGGLE', { pin: LED })
    expect(String(await level())).toMatch(/Low|0|false/i)
    await board.services.device(1, 'IO', 'TOGGLE', { pin: LED })
    expect(String(await level())).toMatch(/High|1|true/i)
  })

  it('SET_LEVEL with a label, then back', async () => {
    await board.services.device(1, 'IO', 'SET_LEVEL', { pin: LED, level: 'Low' })
    expect(String((await board.services.device(1, 'IO', 'GET_LEVEL', { pin: LED })).value)).toMatch(/Low|0|false/i)
    await board.services.device(1, 'IO', 'SET_LEVEL', { pin: LED, level: 'High' })
  })

  it('a pin the chip does not have: ERR_DEV_BAD_PIN', async () => {
    const error = await refused(board.services.device(1, 'IO', 'SET_LEVEL', { pin: 99, level: 'High' }))
    expect(error.record!.cause!.info?.name).toBe('ERR_DEV_BAD_PIN')
  })

  it('SUSPEND refuses a command; RESUME takes it again', async () => {
    await board.services.device(4, 'DEVICE', 'SUSPEND') // the DAC: nothing connected to it
    try {
      const error = await refused(board.services.device(4, 'IO', 'SET_VOLTAGE', { pin: 0, voltage_mV: 100 }))
      expect(error.record!.cause!.info?.name).toBe('ERR_DEV_SUSPENDED')
    } finally {
      await board.services.device(4, 'DEVICE', 'RESUME')
    }
    expect((await board.services.device(4, 'DEVICE', 'INFO')).value).toMatchObject({ suspended: 0 })
  })

  it('a second device with an id in use is refused, and nothing changes', async () => {
    const error = await refused(board.services.create(0, 'gpio_esp'))
    expect(error.record).toBeDefined()
    expect((await board.services.device(0, 'DEVICE', 'INFO')).value).toMatchObject({ type_id: 64 })
  })

  it('a command arguments check: a pin out of the byte range is refused by the encoder, nothing is sent', async () => {
    const before = board.link.commands
    expect(() => board.services.device(1, 'IO', 'SET_LEVEL', { pin: 300, level: 'High' })).toThrow()
    expect(board.link.commands).toBe(before)
  })
})

describe('the console', () => {
  it('logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })
})
