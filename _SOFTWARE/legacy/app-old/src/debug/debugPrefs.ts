import { useSyncExternalStore } from 'react'
import { readStored, writeStored } from '../hooks/useStorage'

/*
 * Debug view preferences, remembered per browser (not part of the project):
 * how long a block's ENO or enable source seen true stays shown true. A tick,
 * and a block acting on it, are true for one VM pass, far shorter than a
 * screen refresh; 0 shows only the latest value.
 */

const KEY = 'runit.debug.holdMs'
export const DEFAULT_HOLD_MS = 200
export const MAX_HOLD_MS = 5000

const clamp = (ms: number): number => (Number.isFinite(ms) ? Math.max(0, Math.min(MAX_HOLD_MS, Math.round(ms))) : DEFAULT_HOLD_MS)

let holdMs = readStored(KEY, (raw) => (Number.isFinite(Number(raw)) ? clamp(Number(raw)) : undefined), DEFAULT_HOLD_MS)
const listeners = new Set<() => void>()

export const setDebugHold = (ms: number): void => {
  holdMs = clamp(ms)
  writeStored(KEY, String(holdMs))
  listeners.forEach((listener) => listener())
}

const subscribe = (listener: () => void): (() => void) => {
  listeners.add(listener)
  return () => listeners.delete(listener)
}

export const getDebugHold = (): number => holdMs

/** The hold time in milliseconds; updates when the setting changes. */
export const useDebugHold = (): number => useSyncExternalStore(subscribe, getDebugHold)
