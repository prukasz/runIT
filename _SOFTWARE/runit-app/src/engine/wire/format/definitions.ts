import type { ExecCommand, ObjType } from './types'

/**
 * Every number the wire format takes from firmware. The engine never writes one as a literal: `generated/` fills this
 * (generated/index.ts reads generated/common/enums.json and generated/services/contract_vm.json), a test can fill it by hand.
 */
export interface WireDefinitions {
  /** `ROUTER_KEY_VM`: the router key a VM command and a telemetry frame start with. */
  readonly routerKey: number
  /** The VM router ops (`VM_OP_*`). */
  readonly ops: Readonly<Record<'reset' | 'open' | 'addObjects' | 'setData' | 'addAccessors' | 'addBlock' | 'subscribe' | 'exec' | 'speed', number>>
  /** `vm_obj_t_e`. */
  readonly objTypes: Readonly<Record<ObjType, number>>
  /** `vm_obj_flag`: the bits of the object head's flag byte the app may set. */
  readonly headFlags: Readonly<Record<'mutable' | 'updResetable' | 'retentive' | 'usrProtected', number>>
  /** `vm_blk_en_mode_e`. */
  readonly enMode: Readonly<Record<'any' | 'all', number>>
  /** `vm_blk_on_error_e`. */
  readonly onError: Readonly<Record<'stop' | 'continue', number>>
  /** `vm_index_kind_e`. */
  readonly idxKinds: Readonly<Record<'literal' | 'ref' | 'name', number>>
  /** `vm_exec_command_e`. */
  readonly exec: Readonly<Record<ExecCommand, number>>
}

/** The C symbol behind each short name above: the single place the two vocabularies meet. Read by generated/index.ts. */
export const WIRE_SYMBOLS = {
  routerKey: { enum: 'router_key', symbol: 'ROUTER_KEY_VM' },
  objTypes: {
    enum: 'vm_obj_t_e',
    symbols: { ptr: 'VM_OBJ_PTR', u8: 'VM_OBJ_U8', u32: 'VM_OBJ_U32', i32: 'VM_OBJ_I32', f32: 'VM_OBJ_F', bool: 'VM_OBJ_B', str: 'VM_OBJ_STR' },
  },
  headFlags: {
    enum: 'vm_obj_flag',
    symbols: {
      mutable: 'VM_OBJ_F_MUTABLE',
      updResetable: 'VM_OBJ_F_UPD_RESETABLE',
      retentive: 'VM_OBJ_F_RETENTIVE',
      usrProtected: 'VM_OBJ_F_USR_PROTECTED',
    },
  },
  enMode: { enum: 'vm_blk_en_mode_e', symbols: { any: 'VM_BLK_EN_ANY', all: 'VM_BLK_EN_ALL' } },
  onError: { enum: 'vm_blk_on_error_e', symbols: { stop: 'VM_BLK_ERR_STOP', continue: 'VM_BLK_ERR_CONTINUE' } },
  idxKinds: { enum: 'vm_index_kind_e', symbols: { literal: 'VM_IDX_LITERAL', ref: 'VM_IDX_REF', name: 'VM_IDX_NAME' } },
  exec: {
    enum: 'vm_exec_command_e',
    symbols: {
      scan: 'VM_EXEC_SCAN_MODE',
      once: 'VM_EXEC_ONCE',
      block: 'VM_EXEC_BLOCK_MODE',
      next: 'VM_EXEC_NEXT',
      restart: 'VM_EXEC_RESET_TO_START',
      normal: 'VM_EXEC_NORMAL_MODE',
      pause: 'VM_EXEC_PAUSE',
      resume: 'VM_EXEC_RESUME',
      reset: 'VM_EXEC_RESET',
      ackFault: 'VM_EXEC_ACK_FAULT',
      retainClear: 'VM_EXEC_RETAIN_CLEAR',
    },
  },
  /** Ids of the commands in generated/services/contract_vm.json (`commands[].id`). */
  ops: {
    reset: 'RESET',
    open: 'OPEN',
    addObjects: 'ADD_OBJECTS',
    setData: 'DATA',
    addAccessors: 'ADD_ACCESSORS',
    addBlock: 'ADD_BLOCK',
    subscribe: 'SUBSCRIBE',
    exec: 'EXEC',
    speed: 'SPEED',
  },
} as const
