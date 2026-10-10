import type { DeviceCatalog, DeviceType, SymbolVariant } from '../../domain/descriptors'
import type { BoardLayout, BoardPlacement, ProjectDevice } from '../../domain/project'
import { scopeSvg } from './boardArt'
import type { BoardArt } from './boardArt'
import { pictureArt, pictureVariants, variantsOf } from './pictureSymbols'

/*
 * Device symbols on the board canvas. A device type's symbol is the SVG the device generator wrote next to its
 * descriptor (`data-structures/devices/<id>.symbol.generated.svg`, a pin group `pin-<name>` per port); a type without
 * one gets a plain box with its name. Symbols are drawn at 10 units to the millimetre.
 */

export const SYMBOL_UNITS_PER_MM = 10
export const SYMBOL_SCOPE = 'device-symbol'

const files = import.meta.glob<string>('../../../../data-structures/devices/*.symbol*.generated.svg', { query: '?raw', import: 'default', eager: true })

const rawSymbols = new Map(Object.entries(files).map(([path, text]) => [path.replace(/^.*\/(.*)\.generated\.svg$/, '$1'), text]))

const escapeXml = (text: string): string => text.replace(/[&<>"]/g, (char) => `&#${char.charCodeAt(0)};`)

/** The box a type without a symbol file gets. */
export const fallbackSymbol = (label: string): BoardArt => ({
  width: 160,
  height: 80,
  markup: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 160 80"><rect x="2" y="2" width="156" height="76" rx="6" fill="var(--panel)" stroke="var(--divider)" stroke-width="2"/><text x="80" y="46" text-anchor="middle" font-size="14" fill="var(--active)" font-family="system-ui, sans-serif">${escapeXml(label)}</text></svg>`,
})

const cache = new Map<string, BoardArt>()

/** The variant a placement shows: the one it names, else the type's first. */
export const variantOf = (type: Pick<DeviceType, 'id' | 'ports' | 'symbolVariants'> | undefined, id: string | undefined): SymbolVariant | undefined => {
  const variants = variantsOf(type)
  return variants.find((variant) => variant.id === id) ?? variants[0]
}

/** The scoped symbol of a device type and variant; a type or variant without an SVG gets the plain box. */
export function symbolOf(type: Pick<DeviceType, 'id' | 'title' | 'ports' | 'symbolVariants'> | undefined, variantId?: string): BoardArt {
  const variant = variantOf(type, variantId)
  const file = variant?.symbol ?? `${type?.id}.symbol`
  let art = cache.get(file)
  if (!art) {
    const picture = variant?.picture && type ? pictureVariants(type).find((entry) => entry.id === variant.id) : undefined
    const raw = rawSymbols.get(file)
    art = picture ? pictureArt(picture.layout, picture.url) : raw ? scopeSvg(raw, SYMBOL_SCOPE) : fallbackSymbol(type?.title ?? file)
    cache.set(file, art)
  }
  return art
}

/** Size on the canvas in millimetres. */
export const sizeMm = (art: BoardArt): { width: number; height: number } => ({ width: art.width / SYMBOL_UNITS_PER_MM, height: art.height / SYMBOL_UNITS_PER_MM })

/** Where one device sits now (its saved placement, or the stacked default), for tools outside the canvas. */
export const placementOf = (devices: readonly ProjectDevice[], catalog: DeviceCatalog, layout: BoardLayout | undefined, ref: string, boardWidthMm: number): BoardPlacement | undefined =>
  placements(devices, layout, boardWidthMm, (device) => sizeMm(symbolOf(catalog.type(device.type), layout?.[device.id]?.variant))).get(ref)

/** Gap between a board and its unplaced devices, and between those devices, in millimetres. */
const GAP = 10

/**
 * Where every device sits: its saved placement, else stacked in a column to the right of the board, in device order
 * (a placed device never moves the others).
 */
export function placements(devices: readonly ProjectDevice[], layout: BoardLayout | undefined, boardWidthMm: number, sizeOf: (device: ProjectDevice) => { width: number; height: number }): Map<string, BoardPlacement> {
  const result = new Map<string, BoardPlacement>()
  let y = 0
  for (const device of devices) {
    const saved = layout?.[device.id]
    if (saved) {
      result.set(device.id, saved)
      continue
    }
    result.set(device.id, { x: boardWidthMm + GAP, y, rotation: 0, mirror: false })
    y += sizeOf(device).height + GAP
  }
  return result
}

export const rotated = (placement: BoardPlacement): BoardPlacement => ({ ...placement, rotation: ((placement.rotation + 90) % 360) as BoardPlacement['rotation'] })
export const mirrored = (placement: BoardPlacement): BoardPlacement => ({ ...placement, mirror: !placement.mirror })

/** CSS transform of a symbol: mirror (left-right) first, then rotate, both about its centre, then move to its millimetre position. */
export const placementTransform = (placement: BoardPlacement, grid: number): string =>
  `translate(${placement.x * grid}px, ${placement.y * grid}px) rotate(${placement.rotation}deg) scaleX(${placement.mirror ? -1 : 1})`
