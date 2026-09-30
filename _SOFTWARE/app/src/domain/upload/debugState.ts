import { formatLiveValue, pathWires, readPath, truthy } from '../compiler'
import type { LiveValues, ObjectLayout } from '../compiler'
import type { BlockWatch } from './debugWatch'

/*
 * What the debug view shows for one block, from telemetry:
 * - `en`: the run gate. `always` = no enable paths (the block runs every pass),
 *   `open` / `closed` from the enable sources' values and the block's mode,
 *   `unknown` until they all reported.
 * - `eno`: true while the block acted on its last run, undefined before it
 *   reported.
 * - the values on its pins and enables, ready to print (undefined = no
 *   telemetry yet).
 */

export type EnableState = 'always' | 'open' | 'closed' | 'unknown'

export interface BlockLive {
  readonly en: EnableState
  readonly eno?: boolean
  readonly enables: readonly (string | undefined)[]
  readonly inputs: readonly (string | undefined)[]
  readonly outputs: readonly (string | undefined)[]
}

/**
 * Whether a wire was true a moment ago (see the debug session). A tick or a
 * block acting on it is true for one pass; a snapshot of the latest value
 * would nearly always miss it.
 */
export type RecentlyTrue = (wire: number) => boolean

export const enableState = (watch: BlockWatch, layout: ObjectLayout, live: LiveValues, recent?: RecentlyTrue): EnableState => {
  if (!watch.enables.length) return 'always'
  const gates = watch.enables.map((path) => {
    const now = truthy(readPath(layout, live, path))
    return now !== true && recent && pathWires(layout, path).some(recent) ? true : now
  })
  const open = gates.filter((gate) => gate === true).length
  const closed = gates.filter((gate) => gate === false).length
  if (watch.enableMode === 'all') return closed > 0 ? 'closed' : open === gates.length ? 'open' : 'unknown'
  return open > 0 ? 'open' : closed === gates.length ? 'closed' : 'unknown'
}

export const blockLive = (watch: BlockWatch, layout: ObjectLayout, live: LiveValues, recent?: RecentlyTrue): BlockLive => {
  const current = watch.eno === undefined ? undefined : truthy(readPath(layout, live, { root: layout.objects[watch.eno]!.node.id }))
  const eno = current === undefined ? undefined : current || (!!recent && recent(watch.eno!))
  return {
    en: enableState(watch, layout, live, recent),
    ...(eno !== undefined ? { eno } : {}),
    enables: watch.enables.map((path) => formatLiveValue(readPath(layout, live, path))),
    inputs: watch.inputs.map((path) => (path ? formatLiveValue(readPath(layout, live, path)) : undefined)),
    outputs: watch.outputs.map((path) => formatLiveValue(readPath(layout, live, path))),
  }
}
