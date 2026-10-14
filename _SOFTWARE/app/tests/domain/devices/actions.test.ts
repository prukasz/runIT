import { describe, expect, it } from 'vitest'
import {
  actionProblems,
  commandFrame,
  createFrame,
  decodeCreateFrame,
  decodeRecords,
  DEVICE_TYPES,
  deviceCommands,
  deviceRecordId,
  deviceType,
  foreignActions,
  newAction,
  newDevice,
  newStep,
  NO_DEVICE,
  recordImages,
  stepFrame,
  stepProblems,
  userActionId,
} from '../../../src/domain/devices'
import type { ProjectAction, StoredDeviceState } from '../../../src/domain/devices'
import { planRecords } from '../../../src/domain/settings'

const pwm = deviceType('device_pca9685')!
const command = (symbol: string) => deviceCommands(pwm).find((entry) => entry.symbol === symbol)!
const empty: StoredDeviceState = { devices: [], setup: [], actions: [], onBoard: [] }

describe('device commands, read from the descriptors', () => {
  it('lists a type\'s contract ops with the arguments the type narrows', () => {
    const level = command('IO_OP_SET_LEVEL')
    expect(level.stored).toBe(true)
    expect(level.op.fields.map((field) => field.name)).toEqual(['pin', 'level'])
    // the PCA9685 has 16 channels, named by the type, not by the contract
    expect(level.op.fields[0]).toMatchObject({ alias: 'PWM channel' })
    expect(level.op.fields[0]!.choices).toHaveLength(16)
    expect(command('IO_OP_GET_LEVEL')).toMatchObject({ returns: true, stored: false })
  })

  it('packs a command as the device router\'s frame: key, device, op, arguments', () => {
    expect([...commandFrame(3, command('IO_OP_SET_LEVEL'), { pin: 5, level: 1 })]).toEqual([5, 3, 0, 0, 1, 5, 1])
    expect([...commandFrame(0x0102, command('SYS_DEVICE_OP_SUSPEND'), {})]).toEqual([5, 2, 1, 2, 0])
  })
})

describe('installing a user device', () => {
  it('writes CREATE with the type and the packed create struct, and reads it back', () => {
    const device = { ...newDevice('device_pca9685', []), deviceId: 20 }
    const install = { ...device.install, i2c_addr: 0x41, 'oe_pin.device': 0, 'oe_pin.pin': 7 }
    const frame = createFrame({ ...device, install }, pwm)
    expect([...frame.subarray(0, 5)]).toEqual([5, 20, 0, 0, 0])
    expect(frame[5]! | (frame[6]! << 8)).toBe(pwm.type.id)
    expect(frame.length).toBe(7 + pwm.create!.size)
    const back = decodeCreateFrame(frame, DEVICE_TYPES)!
    expect(back).toMatchObject({ deviceId: 20, type: { id: 'device_pca9685' } })
    expect(back.install).toMatchObject({ i2c_addr: 0x41, 'oe_pin.device': 0, 'oe_pin.pin': 7 })
  })

  it('lays arrays of pins out element by element', () => {
    const drv = deviceType('device_drv8962')!
    const device = newDevice('device_drv8962', [])
    const install = { ...device.install, 'pin[3].device': 2, 'pin[3].pin': 9, 'current_limit_mA[2]': 1500 }
    const back = decodeCreateFrame(createFrame({ ...device, install }, drv), DEVICE_TYPES)!
    expect(back.install).toMatchObject({ 'pin[3].device': 2, 'pin[3].pin': 9, 'current_limit_mA[2]': 1500, 'pin[4].device': NO_DEVICE })
  })
})

describe('default settings and actions are records', () => {
  const stateWith = (): StoredDeviceState => {
    const servo = { ...newDevice('device_servo', []), deviceId: 20 }
    const setup = [newStep(3, command('IO_OP_SET_LEVEL')), newStep(20, deviceCommands(deviceType('device_servo')).find((entry) => entry.stored && entry.op.fields.length)!)]
    const action: ProjectAction = { ...newAction([]), steps: [{ ...newStep(NO_DEVICE, deviceCommands(undefined)[0] ?? command('SYS_DEVICE_OP_SUSPEND')), op: 'SYS_DEVICE_OP_SUSPEND' }] }
    return { devices: [servo], setup, actions: [action], onBoard: [] }
  }

  it('a device record holds CREATE then its defaults; the board\'s device holds only the defaults; an action holds its steps', () => {
    const state = stateWith()
    const images = recordImages(state)
    expect(images.map((image) => image.id).sort()).toEqual([deviceRecordId(3), deviceRecordId(20), userActionId(state.actions[0]!.actionId)].sort())
    const servo = images.find((image) => image.id === deviceRecordId(20))!
    expect(servo.frames).toHaveLength(2)
    expect(servo.frames[0]![3]).toBe(0)
    expect(images.find((image) => image.id === deviceRecordId(3))!.frames).toHaveLength(1)
    // an action may sweep every device
    expect([...images.find((image) => image.id === userActionId(state.actions[0]!.actionId))!.frames[0]!]).toEqual([5, 0xff, 0xff, 2, 0])
  })

  it('reads the records back into the same defaults, devices and actions', () => {
    const state = stateWith()
    const records = new Map(recordImages(state).map((image) => [image.id, image.frames]))
    const back = decodeRecords(records, { devices: [], actions: [] })
    expect(back.devices).toHaveLength(1)
    expect(back.devices[0]).toMatchObject({ deviceId: 20, type: 'device_servo' })
    expect(back.setup.map((step) => [step.device, step.op])).toEqual(state.setup.map((step) => [step.device, step.op]))
    expect(back.setup.map((step) => step.values)).toEqual(state.setup.map((step) => ({ ...step.values })))
    expect(back.actions[0]).toMatchObject({ actionId: state.actions[0]!.actionId })
    expect(back.actions[0]!.steps[0]).toMatchObject({ device: NO_DEVICE, op: 'SYS_DEVICE_OP_SUSPEND' })
    // the same project writes the same frames again
    const again = recordImages({ ...back, onBoard: [] })
    expect(again.map((image) => [image.id, image.frames.map((frame) => [...frame])])).toEqual(recordImages(state).map((image) => [image.id, image.frames.map((frame) => [...frame])]))
  })

  it('removes the records a project no longer has, but only the actions it put on the board', () => {
    const state: StoredDeviceState = { ...empty, onBoard: [userActionId(7)] }
    const stored = new Set([deviceRecordId(20), userActionId(7), userActionId(3)])
    const gone = recordImages(state, stored).filter((image) => !image.frames.length).map((image) => image.id).sort()
    expect(gone).toEqual([deviceRecordId(20), userActionId(7)].sort())
  })

  it('refuses a step\'s bad value and an action ID taken by the board or another action', () => {
    const step = { ...newStep(3, command('IO_OP_SET_LEVEL')), values: { pin: 99, level: 1 } }
    expect(stepProblems(step, [])).toEqual([expect.stringContaining('PWM channel 99')])
    expect(stepFrame({ ...step, device: 99 }, [])).toBeUndefined()
    const a = { ...newAction([]), actionId: 4 }
    const b = { ...newAction([a]), actionId: 4 }
    expect(actionProblems({ devices: [], setup: [], actions: [a, b] })).toHaveLength(2)
    expect(foreignActions({ ...empty, actions: [a] }, new Set([userActionId(4)]))).toEqual([userActionId(4)])
  })

  it('a save records each one without running it, and removes a record only when the board lists it', () => {
    const state = stateWith()
    const steps = planRecords(recordImages(state))
    const starts = steps.filter((step) => step.label.startsWith('Record '))
    expect(starts).toHaveLength(3)
    // RECORD_START {id u32, execute u8 = 0}
    expect(starts.every((step) => step.payload![4] === 0)).toBe(true)
    expect(steps.filter((step) => step.recorded)).toHaveLength(4)
    const empty = [{ label: 'old', id: deviceRecordId(30), frames: [] }]
    expect(planRecords(empty, new Set())).toEqual([])
    expect(planRecords(empty, new Set([deviceRecordId(30)]))).toHaveLength(1)
  })
})
