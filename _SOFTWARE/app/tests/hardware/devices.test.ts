/**
 * The Devices tab's records on the real board over its console UART: a device's install and default settings and an action are stored
 * (recorded, not run), read back frame by frame, decoded into the project's form, and removed again.
 *
 *   $env:RUNIT_PORT = 'COM9'; npx vitest run tests/hardware/devices
 */
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { routerKey, routerOp } from '../../src/backend/catalog'
import { RouterClient } from '../../src/backend/protocol/client'
import type { ErrorRecord } from '../../src/backend/protocol/frames'
import { decodeRecords, deviceCommands, deviceRecordId, deviceType, newAction, newDevice, newStep, recordImages, userActionId } from '../../src/domain/devices'
import type { StoredDeviceState } from '../../src/domain/devices'
import { planRecords } from '../../src/domain/settings'
import type { SettingsStep } from '../../src/domain/settings'
import { UartLink } from './uartLink'

const port = process.env.RUNIT_PORT
const ACTIONS = 'ROUTER_KEY_ACTIONS'
const idBytes = (id: number): Uint8Array => Uint8Array.of(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff, (id >>> 24) & 0xff)
const u32 = (data: Uint8Array, at: number): number => new DataView(data.buffer, data.byteOffset).getUint32(at, true)

const pwm = deviceType('device_pca9685')!
const servo = deviceType('device_servo')!
const level = deviceCommands(pwm).find((command) => command.symbol === 'IO_OP_SET_LEVEL')!
const angle = deviceCommands(servo).find((command) => command.stored && command.op.fields.length > 0)!

const project = (): StoredDeviceState => {
  const device = { ...newDevice('device_servo', []), deviceId: 21, install: { ...newDevice('device_servo', []).install, 'pwm_pin.device': 3, 'pwm_pin.pin': 4 } }
  const action = { ...newAction([]), actionId: 40, steps: [newStep(3, level), newStep(21, angle)] }
  return {
    devices: [device],
    setup: [{ ...newStep(3, level), values: { pin: 2, level: 1 } }, newStep(21, angle)],
    actions: [action],
    onBoard: [],
  }
}

describe.runIf(port)('the devices\' records on the board', () => {
  let link: UartLink
  let client: RouterClient

  beforeAll(async () => {
    link = new UartLink(port!)
    await link.open()
    client = new RouterClient(link, { timeoutMs: 4000 })
  }, 20000)

  afterAll(async () => {
    client?.close()
    await link?.close()
  })

  const run = async (steps: readonly SettingsStep[]) => {
    for (const step of steps) {
      try {
        await client.request(step.keys, step.payload, { timeoutMs: 4000 })
      } catch (error) {
        if (!step.tolerant) throw new Error(`${step.label}: ${error instanceof Error ? error.message : String(error)}`)
      }
    }
  }

  const stored = async (): Promise<Set<number>> => {
    const ids = new Set<number>()
    let start = 1
    for (;;) {
      const page = await client.request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_LIST')], idBytes(start))
      const listed = Math.floor((page.length - 2) / 4)
      let last = start
      for (let index = 0; index < listed; index++) {
        last = u32(page, 2 + index * 4)
        if (last < 0x03000000) ids.add(last)
      }
      if (listed === 0 || last >= 0x03000000) return ids
      start = last + 1
    }
  }

  const read = async (id: number): Promise<Uint8Array[]> => {
    const [info] = await client.requestMany([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_INFO')], idBytes(id), 1)
    const count = info![2]! | (info![3]! << 8)
    return client.requestMany([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_READ')], idBytes(id), count)
  }

  it('stores the install, the default settings and an action, reads them back as the same project, and removes them', async () => {
    const errors: ErrorRecord[] = []
    const off = client.onError((record) => errors.push(record))
    const state = project()

    const listed = await stored()
    await run(planRecords(recordImages(state, listed), listed))

    const after = await stored()
    expect([deviceRecordId(3), deviceRecordId(21), userActionId(40)].every((id) => after.has(id))).toBe(true)

    const records = new Map<number, Uint8Array[]>()
    for (const id of [deviceRecordId(3), deviceRecordId(21), userActionId(40)]) records.set(id, await read(id))
    const back = decodeRecords(records, { devices: [], actions: [] })
    expect(back.devices.map((device) => [device.deviceId, device.type, device.install['pwm_pin.device'], device.install['pwm_pin.pin']])).toEqual([[21, 'device_servo', 3, 4]])
    expect(back.setup.map((step) => [step.device, step.op, step.values])).toEqual(state.setup.map((step) => [step.device, step.op, { ...step.values }]))
    expect(back.actions.map((action) => [action.actionId, action.steps.length])).toEqual([[40, 2]])

    // the project without them removes them (the action only because this project put it on the board)
    const gone = recordImages({ devices: [], setup: [], actions: [], onBoard: [userActionId(40)] }, after).filter((image) => !image.frames.length)
    await run(planRecords(gone, after))
    const left = await stored()
    expect([deviceRecordId(3), deviceRecordId(21), userActionId(40)].some((id) => left.has(id))).toBe(false)

    await new Promise((resolve) => setTimeout(resolve, 300))
    off()
    expect(errors.map((record) => client.catalog.rootCause(record))).toEqual([])
  }, 60000)
})
