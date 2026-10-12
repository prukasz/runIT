import { describe, expect, it } from 'vitest'
import { enumValue, member, routerKey, routerOp, values } from '../../src/backend/catalog'
import { STREAM, STATUS_ERROR, FLAG_URGENT } from '../../src/backend/protocol/frames'
import { FLAG, KEY_VM, MEMORY_WIDTH, OP, T, WIDTH, EN_MODE, ON_ERROR } from '../../src/backend/vm/wire'

describe('numbers come from the generated descriptors, by symbol', () => {
  it('known values arrive (a guard for the pipeline from the C annotations to the app)', () => {
    expect(KEY_VM).toBe(0x0b)
    expect(OP.reset).toBe(1)
    expect(OP.speed).toBe(9)
    expect(STREAM).toEqual({ telemetry: 2, logs: 3, errors: 4, response: 5, events: 6 })
    expect([STATUS_ERROR, FLAG_URGENT]).toEqual([1, 1])
    expect(FLAG).toEqual({ mutable: 0x01, updResetable: 0x04, retentive: 0x10, usrProtected: 0x40 })
    expect([WIDTH[T.ptr], MEMORY_WIDTH[T.ptr], WIDTH[T.f32], WIDTH[T.bool]]).toEqual([2, 4, 4, 1])
    expect([EN_MODE.any, EN_MODE.all, ON_ERROR.stop, ON_ERROR.continue]).toEqual([0, 1, 0, 1])
  })

  it('every object type has its element sizes', () => {
    for (const type of Object.values(T)) {
      expect(WIDTH[type]).toBeGreaterThan(0)
      expect(MEMORY_WIDTH[type]).toBeGreaterThan(0)
    }
  })

  it('a symbol that is not there stops with a message that names it', () => {
    expect(() => routerKey('ROUTER_KEY_NOPE')).toThrow('ROUTER_KEY_NOPE')
    expect(() => routerOp('ROUTER_KEY_VM', 'VM_OP_NOPE')).toThrow('VM_OP_NOPE')
    expect(() => routerOp('ROUTER_KEY_NOPE', 'X')).toThrow('ROUTER_KEY_NOPE')
    expect(() => enumValue('no_such_enum', 'X')).toThrow('no_such_enum')
    expect(() => member('vm_obj_t_e', 'VM_OBJ_NOPE')).toThrow('VM_OBJ_NOPE')
    expect(() => values('vm_obj_t_e', { x: 'VM_OBJ_NOPE' })).toThrow('VM_OBJ_NOPE')
  })
})
