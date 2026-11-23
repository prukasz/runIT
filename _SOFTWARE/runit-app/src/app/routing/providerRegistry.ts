import type { ProjectFile } from '../project/projectFile'
import type { LiveProvider } from './routing'
import { PROV_TYPE, providerTypeLabel, uartProviderNames } from './routing'
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
  /** The user's name, else what it is (its type, a way's, a link's board) with a number when several are alike. */
  readonly name: string
}

export interface ProviderRegistry {
  readonly entries: ReadonlyMap<number, ProviderEntry>
  /** Nothing holds the id (`except`: an id the caller itself holds counts as free). */
  readonly isFree: (id: number, except?: number) => boolean
  /** The lowest free id in `from`..`to`. */
  readonly freeFrom: (from: number, to: number) => number | undefined
  /** How the app shows a provider: its name. The id is never shown. */
  readonly name: (id: number) => string
}

export function buildProviderRegistry(inputs: {
  readonly live: readonly LiveProvider[]
  readonly project: ProjectFile
  readonly devices: readonly DeviceInfo[]
}): ProviderRegistry {
  const { live, project, devices } = inputs
  const names = { ...uartProviderNames(devices), ...project.providerNames }
  const held: { id: number; holder: ProviderHolder; base: string; given: string }[] = []
  const seen = new Set<number>()
  const add = (id: number, holder: ProviderHolder, base: string) => {
    if (seen.has(id)) return
    seen.add(id)
    held.push({ id, holder, base, given: names[String(id)]?.trim() ?? '' })
  }
  for (const p of live) add(p.id, 'board', providerTypeLabel(p.type))
  for (const b of project.boards ?? []) add(b.provider, 'link', `Link to ${b.name}`)
  for (const s of project.settings.gatt ?? []) for (const c of s.characteristics) add(c.provider, 'way', providerTypeLabel(PROV_TYPE.ble))
  // Unnamed providers of one kind are told apart by a number, in the order of their ids.
  const kinds = new Map<string, number[]>()
  for (const h of held.filter((o) => !o.given).sort((a, b) => a.id - b.id)) kinds.set(h.base, [...(kinds.get(h.base) ?? []), h.id])
  const entries = new Map<number, ProviderEntry>(held.map((h) => {
    const same = kinds.get(h.base) ?? []
    const name = h.given || (same.length > 1 ? `${h.base} ${same.indexOf(h.id) + 1}` : h.base)
    return [h.id, { id: h.id, holder: h.holder, name }]
  }))
  return {
    entries,
    isFree: (id, except) => id === except || !entries.has(id),
    freeFrom: (from, to) => { for (let id = from; id <= to; id++) if (!entries.has(id)) return id; return undefined },
    name: (id) => entries.get(id)?.name ?? names[String(id)]?.trim() ?? 'Provider',
  }
}
