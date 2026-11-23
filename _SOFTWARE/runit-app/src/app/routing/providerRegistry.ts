import type { ProjectFile } from '../project/projectFile'
import type { LiveProvider } from './routing'
import { providerTypeLabel, uartProviderNames } from './routing'
import type { DeviceInfo } from '../devices/devices'

/*
 * The app's one register of provider ids: every id something holds, whoever holds it. A screen that picks or makes a provider
 * asks the register (`isFree`, `freeFrom`), never its own list, so two parts of the app cannot give one id twice.
 *
 *   board    a provider the board has now (its own, a UART's, one made over the link)
 *   way      a Bluetooth way of the project (made at Apply / Write)
 *   link     the link to another board of the project
 */

export type ProviderHolder = 'board' | 'way' | 'link'

export interface ProviderEntry {
  readonly id: number
  readonly holder: ProviderHolder
  /** The user's name, else what the holder is called. */
  readonly label: string
}

export interface ProviderRegistry {
  readonly entries: ReadonlyMap<number, ProviderEntry>
  /** Nothing holds the id (`except`: an id the caller itself holds counts as free). */
  readonly isFree: (id: number, except?: number) => boolean
  /** The lowest free id in `from`..`to`. */
  readonly freeFrom: (from: number, to: number) => number | undefined
  /** The ids in `from`..`to` that are free, and `keep` (the caller's own). */
  readonly freeIds: (from: number, to: number, keep?: number) => number[]
  /** The id with its name: `16 · sensor`, else `16`. */
  readonly label: (id: number) => string
}

export function buildProviderRegistry(inputs: {
  readonly live: readonly LiveProvider[]
  readonly project: ProjectFile
  readonly devices: readonly DeviceInfo[]
}): ProviderRegistry {
  const { live, project, devices } = inputs
  const names = { ...uartProviderNames(devices), ...project.providerNames }
  const entries = new Map<number, ProviderEntry>()
  const add = (id: number, holder: ProviderHolder, what: string) => {
    if (!entries.has(id)) entries.set(id, { id, holder, label: names[String(id)]?.trim() || what })
  }
  for (const p of live) add(p.id, 'board', `${providerTypeLabel(p.type)}`)
  for (const b of project.boards ?? []) add(b.provider, 'link', b.name)
  for (const s of project.settings.gatt ?? []) for (const c of s.characteristics) add(c.provider, 'way', c.name || 'Bluetooth way')
  const isFree = (id: number, except?: number) => id === except || !entries.has(id)
  const freeIds = (from: number, to: number, keep?: number) => {
    const ids: number[] = []
    for (let id = from; id <= to; id++) if (isFree(id, keep)) ids.push(id)
    return ids
  }
  return {
    entries,
    isFree,
    freeFrom: (from, to) => freeIds(from, to)[0],
    freeIds,
    label: (id) => (names[String(id)]?.trim() ? `${id} · ${names[String(id)]!.trim()}` : String(id)),
  }
}
