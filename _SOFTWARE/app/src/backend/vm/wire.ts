import { enumMembers, routerKey, routerOp, values } from '../catalog'

/**
 * The VM router (ROUTER_KEY_VM): components/vm/vm_core/VM_CORE.MD and loader/vm_wire.h. Every number is read from the generated
 * descriptors by its C symbol (catalog.ts); the short names below are the app's.
 */
export const KEY_VM = routerKey('ROUTER_KEY_VM')

const vmOp = (symbol: string) => routerOp('ROUTER_KEY_VM', symbol)
export const OP = {
  reset: vmOp('VM_OP_RESET'),
  open: vmOp('VM_OP_OPEN'),
  addObjects: vmOp('VM_OP_ADD_OBJECTS'),
  setData: vmOp('VM_OP_SET_DATA'),
  addAccessors: vmOp('VM_OP_ADD_ACCESSORS'),
  addBlock: vmOp('VM_OP_ADD_BLOCK'),
  subscribe: vmOp('VM_OP_SUBSCRIBE'),
  exec: vmOp('VM_OP_EXEC'),
  speed: vmOp('VM_OP_SPEED'),
}

/** `vm_exec_command_e`. */
export const EXEC = values('vm_exec_command_e', {
  scan: 'VM_EXEC_SCAN_MODE', once: 'VM_EXEC_ONCE', block: 'VM_EXEC_BLOCK_MODE', next: 'VM_EXEC_NEXT', restart: 'VM_EXEC_RESET_TO_START',
  normal: 'VM_EXEC_NORMAL_MODE', pause: 'VM_EXEC_PAUSE', resume: 'VM_EXEC_RESUME', reset: 'VM_EXEC_RESET', ackFault: 'VM_EXEC_ACK_FAULT',
  retainClear: 'VM_EXEC_RETAIN_CLEAR',
})

/** `vm_obj_t_e`: the element type of an object. */
export const T = values('vm_obj_t_e', { ptr: 'VM_OBJ_PTR', u8: 'VM_OBJ_U8', u32: 'VM_OBJ_U32', i32: 'VM_OBJ_I32', f32: 'VM_OBJ_F', bool: 'VM_OBJ_B', str: 'VM_OBJ_STR' })
export type ObjType = number

const sizes = (key: 'wire_size' | 'mem_size'): Readonly<Record<number, number>> =>
  Object.fromEntries(
    enumMembers('vm_obj_t_e')
      .filter((m) => m.value !== 0)
      .map((m) => {
        const size = m[key]
        if (size === undefined) throw new Error(`${m.name} has no @${key.replace('_', '-')} (data-structures/enums.json).`)
        return [m.value, size]
      }),
  )

/** Bytes of one element on the wire. A PTR element is a u16 object id on the wire (4 bytes in the board's memory). */
export const WIDTH: Readonly<Record<number, number>> = sizes('wire_size')
/** Bytes of one element in the board's memory: what an object's `payload_size` counts. */
export const MEMORY_WIDTH: Readonly<Record<number, number>> = sizes('mem_size')

/** Object head flags on the wire (the app may set these four: `vm_wire_head_flag`). */
export const FLAG = values('vm_wire_head_flag', {
  mutable: 'VM_WIRE_HEAD_F_MUTABLE', updResetable: 'VM_WIRE_HEAD_F_UPD_RESETABLE', retentive: 'VM_WIRE_HEAD_F_RETENTIVE', usrProtected: 'VM_WIRE_HEAD_F_USR_PROTECTED',
})

/** Id 0xFFFF (`VM_ID_NONE`) means "none": an unwired pin, no ENO. The board refuses it as an id of a new object, accessor or block. */
export const NO_ID = 0xffff
/** Objects the board creates at run time (heap objects) have this bit in their id. */
export const DYNAMIC_ID_BIT = 0x8000

export const EN_MODE = values('vm_blk_en_mode_e', { any: 'VM_BLK_EN_ANY', all: 'VM_BLK_EN_ALL' })
export const ON_ERROR = values('vm_blk_on_error_e', { stop: 'VM_BLK_ERR_STOP', continue: 'VM_BLK_ERR_CONTINUE' })

/** Payload bytes per load frame (the router takes 512). */
export const FRAME_BUDGET = 400
/** Bytes of one value record's data in a load frame. */
export const DATA_CHUNK = 192

export type Value = number | boolean | readonly (number | boolean)[] | Uint8Array

/** One value (or a list) as the bytes of an object of type `type`. */
export const encodeValue = (type: ObjType, value: Value): Uint8Array => {
  if (value instanceof Uint8Array) return value
  const items = Array.isArray(value) ? (value as readonly (number | boolean)[]) : [value as number | boolean]
  const out = new Uint8Array(items.length * WIDTH[type])
  const view = new DataView(out.buffer)
  items.forEach((item, i) => {
    const at = i * WIDTH[type]
    const n = typeof item === 'boolean' ? (item ? 1 : 0) : item
    switch (type) {
      case T.u8: case T.str: view.setUint8(at, n); break
      case T.bool: view.setUint8(at, n ? 1 : 0); break
      case T.u32: view.setUint32(at, n >>> 0, true); break
      case T.i32: view.setInt32(at, n, true); break
      case T.f32: view.setFloat32(at, n, true); break
      case T.ptr: view.setUint16(at, n, true); break
    }
  })
  return out
}

/** The value of an object's bytes: a number (bool: boolean) for one element, an array for several. Strings stay bytes. */
export const decodeValue = (type: ObjType, bytes: Uint8Array): number | boolean | number[] | Uint8Array => {
  if (type === T.str) return bytes
  const width = WIDTH[type]
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const out: (number | boolean)[] = []
  for (let at = 0; at + width <= bytes.length; at += width) {
    switch (type) {
      case T.u8: out.push(view.getUint8(at)); break
      case T.bool: out.push(view.getUint8(at) !== 0); break
      case T.u32: out.push(view.getUint32(at, true)); break
      case T.i32: out.push(view.getInt32(at, true)); break
      case T.f32: out.push(view.getFloat32(at, true)); break
      case T.ptr: out.push(view.getUint16(at, true)); break
    }
  }
  return (out.length === 1 ? out[0] : out) as number | boolean | number[]
}

/** One SET_DATA record: `[id u16][start u16][byte_len u16][data]`; `start` counts elements. */
export const dataRecord = (id: number, data: Uint8Array, start = 0): Uint8Array => {
  const out = new Uint8Array(6 + data.length)
  const view = new DataView(out.buffer)
  view.setUint16(0, id, true)
  view.setUint16(2, start, true)
  view.setUint16(4, data.length, true)
  out.set(data, 6)
  return out
}

/** A batch body: `[count u8]` and the records. */
export const batch = (records: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(1 + records.reduce((n, r) => n + r.length, 0))
  out[0] = records.length
  let at = 1
  for (const record of records) {
    out.set(record, at)
    at += record.length
  }
  return out
}
