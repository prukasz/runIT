import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog, runitVmCatalog } from '../../../src/domain/descriptors'
import { resolveDeviceValue } from '../../../src/domain/devices'
import { blockShape, pinValueOf } from '../../../src/canvas/blocks/blockView'
import type { ProjectDevice } from '../../../src/domain/project'

const catalog = runitDeviceCatalog()
const servo = catalog.type('device_servo')!
const device = (install: Record<string, number>): ProjectDevice => ({ id: 's', deviceId: 14, type: 'device_servo', name: 'Servo', tags: [], install })

describe('device values', () => {
  it('reads what a servo position means from its own settings, the first matching variant winning', () => {
    const base = { servo_type: 0, byte_mode: 0, angle_min_deg: -45, angle_max_deg: 45 }
    expect(resolveDeviceValue(servo, base, 'POSITION')).toMatchObject({ title: 'Angle', unit: 'deg', min: -45, max: 45, hint: '-45 to 45 deg' })
    expect(resolveDeviceValue(servo, { ...base, servo_type: 1 }, 'POSITION')).toMatchObject({ title: 'Speed', unit: '%', min: -100, max: 100, hint: '-100 to 100 %' })
    // Byte steering is listed first, so it wins over the type.
    expect(resolveDeviceValue(servo, { ...base, servo_type: 1, byte_mode: 1 }, 'POSITION')).toMatchObject({ title: 'Byte', min: 0, max: 255, hint: '0 to 255' })
    expect(resolveDeviceValue(servo, base, 'NOPE')).toBeUndefined()
  })
})

describe('the servo block pin', () => {
  const vm = runitVmCatalog()
  const type = vm.block('SERVO')!
  const pin = type.inputs.pins[0]

  it('is named after what the chosen servo takes, with its range beside the title', () => {
    expect(pin?.valueSpec).toBe('POSITION')
    const block = { id: 'b', type: 'SERVO', settings: { device_id: 14 } }
    expect(blockShape(type, block, true, [device({ servo_type: 1, byte_mode: 1 })]).inputs[0]).toMatchObject({ title: 'Byte', hint: '0 to 255' })
    expect(blockShape(type, block, true, [device({ servo_type: 1, byte_mode: 0 })]).inputs[0]).toMatchObject({ title: 'Speed', hint: '-100 to 100 %' })
    expect(pinValueOf(block, pin, [device({ servo_type: 0, byte_mode: 0 })])).toMatchObject({ title: 'Angle' })
  })

  it('keeps the plain name while no servo is chosen', () => {
    expect(blockShape(type, { id: 'b', type: 'SERVO' }, true, [device({ servo_type: 1 })]).inputs[0]).toMatchObject({ title: 'Position' })
    expect(blockShape(type, { id: 'b', type: 'SERVO' }, true, [device({ servo_type: 1 })]).inputs[0]?.hint).toBeUndefined()
  })
})
