import { decodeVmElements } from '../decoder'
import type { ObjectPath } from '../project'
import type { ObjectLayout } from './objects'

/*
 * Live values against an uploaded program: what an ObjectPath reaches on the
 * board and what telemetry last said about it. Used by the debug view to write
 * a value next to each pin. A dynamic step (`bank[sel]`) reads its position
 * live; with no value for it yet, every place it could lead is a candidate
 * (which is also what to subscribe to).
 */

/** Telemetry as it arrives: the last bytes per object wire ID. */
export type LiveValues = ReadonlyMap<number, Uint8Array>

/** A value object on the board and, when the path picks one, the element. */
export interface LiveTarget {
  readonly wire: number
  readonly element?: number
}

export type LiveValue = number | boolean | string | readonly (number | boolean)[]

/** No path leads to more places than this (a dynamic step over a folder). */
const MAX_CANDIDATES = 32

export const pathTargets = (layout: ObjectLayout, path: ObjectPath, dynamicIndex?: (path: ObjectPath) => number | undefined): LiveTarget[] => {
  const root = layout.wireIdOf.get(path.root)
  if (root === undefined) return []
  let candidates: LiveTarget[] = [{ wire: root }]
  for (const step of path.steps ?? []) {
    const next: LiveTarget[] = []
    for (const candidate of candidates) {
      const placed = layout.objects[candidate.wire]
      if (!placed || candidate.element !== undefined) continue
      const dynamic = step.kind === 'dynamic' ? dynamicIndex?.(step.index) : undefined
      if (placed.children) {
        if (step.kind === 'name') {
          for (const child of placed.children) if (layout.objects[child]?.node.name === step.name) next.push({ wire: child })
        } else if (step.kind === 'index' || dynamic !== undefined) {
          const child = placed.children[step.kind === 'index' ? step.index : dynamic!]
          if (child !== undefined) next.push({ wire: child })
        } else {
          for (const child of placed.children) next.push({ wire: child })
        }
      } else if (step.kind === 'index') next.push({ wire: candidate.wire, element: step.index })
      else if (step.kind === 'dynamic') next.push(dynamic !== undefined ? { wire: candidate.wire, element: dynamic } : candidate)
    }
    candidates = next.slice(0, MAX_CANDIDATES)
  }
  return candidates
}

/**
 * Wire IDs whose values a path's live value depends on: the value objects it
 * can reach and, for its dynamic steps, the objects that hold the positions.
 */
export const pathWires = (layout: ObjectLayout, path: ObjectPath): number[] => {
  const wires = new Set<number>()
  const visit = (current: ObjectPath) => {
    for (const target of pathTargets(layout, current)) if (!layout.objects[target.wire]?.children) wires.add(target.wire)
    for (const step of current.steps ?? []) if (step.kind === 'dynamic') visit(step.index)
  }
  visit(path)
  return [...wires]
}

const asNumber = (value: LiveValue | undefined): number | undefined => {
  if (typeof value === 'number') return value
  if (typeof value === 'boolean') return value ? 1 : 0
  return undefined
}

/** What a path reads now, or undefined while there's no telemetry for it (or it names a folder). */
export const readPath = (layout: ObjectLayout, live: LiveValues, path: ObjectPath, depth = 0): LiveValue | undefined => {
  if (depth > 4) return undefined
  // A dynamic position with no value yet: which element is read isn't known, so nothing is shown.
  let unresolved = false
  const targets = pathTargets(layout, path, (index) => {
    const position = asNumber(readPath(layout, live, index, depth + 1))
    if (position === undefined) unresolved = true
    return position
  })
  if (unresolved || targets.length !== 1) return undefined
  const target = targets[0]!
  const placed = layout.objects[target.wire]
  const raw = live.get(target.wire)
  if (!placed || placed.children || !raw) return undefined
  const elements = decodeVmElements(placed.type, raw)
  if (typeof elements === 'string') return elements
  if (target.element !== undefined) return elements[target.element]
  return elements.length === 1 ? elements[0] : elements
}

/** True / false for a bool or a number (non-zero), undefined for anything else. */
export const truthy = (value: LiveValue | undefined): boolean | undefined => {
  const number = asNumber(value)
  return number === undefined ? undefined : number !== 0
}

const scalar = (value: number | boolean): string => {
  if (typeof value === 'boolean') return value ? '1' : '0'
  if (Number.isInteger(value)) return String(value)
  const text = Number(value.toPrecision(4)).toString()
  return Math.abs(value) >= 1e5 || (Math.abs(value) < 1e-3 && value !== 0) ? value.toExponential(2) : text
}

/** A live value short enough for a pin: numbers to 4 digits, bools as 1 / 0, arrays cut after `limit` elements. */
export const formatLiveValue = (value: LiveValue | undefined, limit = 3): string | undefined => {
  if (value === undefined) return undefined
  if (typeof value === 'string') return value.length > 10 ? `"${value.slice(0, 9)}…"` : `"${value}"`
  if (typeof value === 'number' || typeof value === 'boolean') return scalar(value)
  return `[${value.slice(0, limit).map(scalar).join(' ')}${value.length > limit ? ' …' : ''}]`
}
