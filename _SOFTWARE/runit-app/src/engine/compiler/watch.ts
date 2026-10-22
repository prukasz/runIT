import { DEFAULT_LIMITS } from '../wire'
import type { VmLimits } from '../wire'
import type { CompileResult } from './compile'

/*
 * What to ask the board to send while the program runs (SUBSCRIBE, VM_CORE.MD "Telemetry is the same command"). The board follows a
 * subscribed object and everything its PTR children reach (8 levels), so a folder is one id for all of its variables.
 *
 *   flow   what a live view shows: every variable, each block's When done (the block acted on this pass) and the value of every
 *          block output (the block's own objects included, last: the first left out when the board takes no more). Needs a
 *          program compiled with `allEnableOuts`, or only the blocks something reads have a When done.
 *   full   a debug view: the same.
 *
 * A target is a name for what an object stands for in the project (`watchKey`): a variable, a block's output, a block's When done.
 * One object can be several (a block that writes a variable).
 */

export type WatchDetail = 'flow' | 'full'

export const variableKey = (variable: string): string => `variable:${variable}`
export const outputKey = (block: string, index: number): string => `${block}:out${index}`
export const doneKey = (block: string): string => `${block}:eno`

export interface WatchPlan {
  /** The object ids to subscribe to, most useful first, within what the board takes. */
  readonly ids: readonly number[]
  /** Object id -> what it stands for (`variableKey`, `outputKey`, `doneKey`). */
  readonly targets: ReadonlyMap<number, readonly string[]>
  /** Objects left out because the board would not follow them (subscribed ids or tracked objects past its limits). */
  readonly dropped: number
}

/** How deep the board follows PTR children (`vm_sub.h`). */
const FOLLOW_DEPTH = 8

export const watchPlan = (result: CompileResult, detail: WatchDetail, limits: VmLimits = DEFAULT_LIMITS): WatchPlan => {
  const ir = result.ir
  if (!ir) return { ids: [], targets: new Map(), dropped: 0 }
  const targets = new Map<number, string[]>()
  const wanted: number[] = []
  const want = (object: number, key: string) => {
    const keys = targets.get(object)
    if (keys) keys.push(key)
    else {
      targets.set(object, [key])
      wanted.push(object)
    }
  }
  // Priority: the flow, then the variables, then the block outputs.
  for (const [key, object] of result.ids.outputs) if (key.endsWith(':eno')) want(object, doneKey(key.slice(0, -4)))
  for (const [variable, object] of result.ids.variables) want(object, variableKey(variable))
  // An output the user named is the variable's object: its output key goes to the same object.
  for (const [key, object] of result.ids.outputs) if (!key.endsWith(':eno')) {
    const at = key.lastIndexOf(':out')
    want(object, outputKey(key.slice(0, at), Number(key.slice(at + 4))))
  }

  const children = new Map<number, number[]>()
  const ptr = new Set(ir.objects.filter((o) => o.type === 'ptr').map((o) => o.id))
  for (const d of ir.data) {
    if (!ptr.has(d.id)) continue
    const list = children.get(d.id) ?? []
    const view = new DataView(d.data.buffer, d.data.byteOffset, d.data.byteLength)
    for (let at = 0; at + 2 <= d.data.length; at += 2) list[d.start + at / 2] = view.getUint16(at, true)
    children.set(d.id, list)
  }
  const covered = new Set([...children.values()].flat())
  const reach = (root: number, into: Set<number>) => {
    const walk = (object: number, depth: number) => {
      into.add(object)
      if (depth < FOLLOW_DEPTH) for (const child of children.get(object) ?? []) if (child !== undefined && !into.has(child)) walk(child, depth + 1)
    }
    walk(root, 0)
  }

  const ids: number[] = []
  const tracked = new Set<number>()
  let dropped = 0
  for (const object of wanted) {
    if (covered.has(object)) continue
    const next = new Set(tracked)
    reach(object, next)
    if (ids.length >= limits.subscribeMax || next.size > limits.subscribeTracked) {
      dropped += 1
      continue
    }
    ids.push(object)
    for (const o of next) tracked.add(o)
  }
  return { ids, targets, dropped }
}
