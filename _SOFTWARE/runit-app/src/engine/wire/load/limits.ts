/**
 * What the board refuses, as numbers. They are options of the firmware's Kconfig that `templates/generate_limits.py`
 * publishes in generated/common/limits.json (`#@limit` in runit-esp/components/vm/vm_core/Kconfig and runit-esp/components/core/router/Kconfig).
 * `createEngine` reads that file; `DEFAULT_LIMITS` is the same numbers for code that builds an `Engine` by hand, and a test
 * keeps the two equal. A board built with another configuration publishes its own file, or the app passes `limits`.
 */
export interface VmLimits {
  /** `CONFIG_VM_BLOCK_MAX_IN`, `_OUT`, `_EN`: pins per block. */
  readonly maxIn: number
  readonly maxOut: number
  readonly maxEn: number
  /** `CONFIG_VM_STORE_MAX_POOL`: the largest program arena, bytes. */
  readonly storeMaxPool: number
  /** `CONFIG_VM_ACCESSOR_MAX_DEPTH`: the longest chain of accessors reading accessors. */
  readonly accessorMaxDepth: number
  /** `CONFIG_VM_SUB_MAX_SUBSCRIBERS`: object ids one SUBSCRIBE may carry. */
  readonly subscribeMax: number
  /** `CONFIG_VM_SUB_MAX_TRACKED`: objects telemetry follows (the subscribed ones and what their PTR children reach). */
  readonly subscribeTracked: number
  /** `CONFIG_VM_EXEC_MAX_SPAN_DEPTH`: nested loops. */
  readonly spanDepthMax: number
  /** `CONFIG_VM_RETAIN_MAX_BYTES`: the stored record of retained values (`2 + 4 + name + value` per object). */
  readonly retainMaxBytes: number
  /**
   * `CONFIG_ROUTER_FRAME_MAX`: the largest frame the router takes from a link. A link may carry less right now (a BLE MTU,
   * `link->max_frame` in router_dispatch.c): pass that figure here and the engine packs frames to fit.
   */
  readonly frameMax: number
  /**
   * Payload bytes the packer fills a load frame with. A policy of the app, not a limit of the board: the host suite's builder
   * (`FRAME_BUDGET` in `.claude/skills/runit-esp/scripts/vm_lib.py`) keeps 400 of the 512, and the golden frames are made with
   * it. The frames never go past what the board's frame holds (`frameCapacity`).
   */
  readonly frameBudget: number
  /** Bytes of one value record's data in a load frame; also a policy (`vm_lib.py` cuts a value at 192). */
  readonly dataChunk: number
}

/**
 * What the router puts in front of a VM frame's payload: `[seq][flags]` (`REQ_HDR` in router_dispatch.c), the root key
 * (`ROUTER_KEY_VM`) and the op, the two keys of a VM command. A test reads `REQ_HDR` from the C.
 */
export const FRAME_OVERHEAD = 4

/** The most payload one frame of this board holds. */
export const frameCapacity = (limits: VmLimits): number => limits.frameMax - FRAME_OVERHEAD

/** Payload bytes a batch frame is filled to: the policy, or less when the board's frame is smaller. A batch frame starts with `u8 count`. */
export const frameFill = (limits: VmLimits): number => Math.min(limits.frameBudget, frameCapacity(limits) - 1)

/** Bytes of a value's data one record carries: the policy, less when the frame is small (the record's own 6 bytes and the count are in front), whole elements only. */
export const dataChunk = (limits: VmLimits, elementWidth: number): number => {
  const fits = Math.min(limits.dataChunk, frameCapacity(limits) - 1 - 6)
  return Math.max(Math.floor(fits / elementWidth), 1) * elementWidth
}

/**
 * The Kconfig name (without `CONFIG_`) each field takes from generated/common/limits.json, which `generated/limits.ts`
 * reads. `frameBudget` and `dataChunk` have none: they are the app's own packing policy.
 */
export const LIMIT_SYMBOLS = {
  maxIn: 'VM_BLOCK_MAX_IN',
  maxOut: 'VM_BLOCK_MAX_OUT',
  maxEn: 'VM_BLOCK_MAX_EN',
  storeMaxPool: 'VM_STORE_MAX_POOL',
  accessorMaxDepth: 'VM_ACCESSOR_MAX_DEPTH',
  subscribeMax: 'VM_SUB_MAX_SUBSCRIBERS',
  subscribeTracked: 'VM_SUB_MAX_TRACKED',
  spanDepthMax: 'VM_EXEC_MAX_SPAN_DEPTH',
  retainMaxBytes: 'VM_RETAIN_MAX_BYTES',
  frameMax: 'ROUTER_FRAME_MAX',
} as const satisfies Partial<Record<keyof VmLimits, string>>

export const DEFAULT_LIMITS: VmLimits = {
  maxIn: 16,
  maxOut: 16,
  maxEn: 16,
  storeMaxPool: 131072,
  accessorMaxDepth: 8,
  subscribeMax: 128,
  subscribeTracked: 256,
  spanDepthMax: 4,
  retainMaxBytes: 1024,
  frameMax: 512,
  frameBudget: 400,
  dataChunk: 192,
}
