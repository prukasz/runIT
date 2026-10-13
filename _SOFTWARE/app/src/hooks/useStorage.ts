import { useEffect, useState } from 'react'

/** Browser storage is a convenience: it can be missing, full or unreadable, and then the app runs on its defaults. */

/** The stored text run through `parse`; `fallback` when nothing is stored, storage is unavailable, or `parse` throws or returns undefined. */
export function readStored<T>(key: string, parse: (raw: string) => T | undefined, fallback: T): T {
  try {
    const raw = localStorage.getItem(key)
    if (raw) {
      const value = parse(raw)
      if (value !== undefined) return value
    }
  } catch { /* Storage unavailable or unreadable: use the fallback. */ }
  return fallback
}

export function writeStored(key: string, text: string): void {
  try { localStorage.setItem(key, text) } catch { /* A preference or draft only; the project file keeps the data. */ }
}

/** Save `value` under `key` whenever it changes. `serialize` may throw; nothing is written then. */
export function usePersistEffect<T>(key: string, value: T, serialize: (value: T) => string = JSON.stringify): void {
  useEffect(() => {
    try { localStorage.setItem(key, serialize(value)) } catch { /* Kept by the project file. */ }
    // `serialize` is a fixed function of the caller; only the value's change is a reason to save.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, value])
}

/** A remembered choice among fixed strings (a tab, a mode, a theme). */
export function usePersistedChoice<T extends string>(key: string, allowed: readonly T[], fallback: T): [T, (value: T) => void] {
  const [value, setValue] = useState<T>(() => readStored(key, (raw) => allowed.find((entry) => entry === raw), fallback))
  return [value, (next) => { setValue(next); writeStored(key, next) }]
}

/** A remembered on / off preference, stored as `on` / `off`. */
export function usePersistedFlag(key: string, fallback: boolean): [boolean, (value: boolean) => void] {
  const [value, setValue] = useState<boolean>(() => readStored(key, (raw) => raw === 'on' ? true : raw === 'off' ? false : undefined, fallback))
  return [value, (next) => { setValue(next); writeStored(key, next ? 'on' : 'off') }]
}
