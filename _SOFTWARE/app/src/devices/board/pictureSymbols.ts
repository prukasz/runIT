import type { DevicePort, DeviceType, SymbolVariant } from '../../domain/descriptors'
import { resolveLayout } from '../../domain/devices/componentLayout'
import { FLOW_SHORT, flowOf } from '../../domain/hardware'
import type { ResolvedLayout } from '../../domain/devices/componentLayout'
import type { BoardArt } from './boardArt'

/*
 * Picture variants of a device (board-canvas-concept.md §4a): the layout files and pictures the anchor editor saved in
 * `components/devices/<device>/app/canvas/<variant>/` are bundled here, and each layout becomes a symbol variant drawn as its picture with
 * a hoverable anchor per pin the variant brings out. Symbols are drawn at 10 units to the millimetre, one grid cell
 * (one millimetre) per cell of the layout.
 */

export const PICTURE_UNITS_PER_CELL = 10
const ANCHOR_RADIUS = 3
/** Label size in symbol units (shown by the canvas when zoomed in, or for the selected device). */
const LABEL_SIZE = 5

const layoutFiles = import.meta.glob<unknown>('../../../../components/devices/*/app/canvas/*/layout.json', { eager: true, import: 'default' })
const imageFiles = import.meta.glob<string>('../../../../components/devices/*/app/canvas/*/*.{svg,png,jpg,jpeg,webp}', { eager: true, query: '?url', import: 'default' })

export interface PictureVariant {
  readonly id: string
  readonly label: string
  readonly layout: ResolvedLayout
  /** Where the bundled picture is served from. */
  readonly url: string
}

const KIND_COLOR: Record<DevicePort['kind'], string> = { io: '#f5a623', bus: '#3b9cff', gnd: '#9aa0a6', supply_in: '#e5484d', supply_out: '#e5484d' }

const escapeXml = (text: string): string => text.replace(/[&<>"]/g, (char) => `&#${char.charCodeAt(0)};`)

/** The symbol of a layout: the picture and, per placed pin, a terminal group `pin-<name>` (hover names it, wires attach to it). */
export function pictureArt(layout: ResolvedLayout, url: string): BoardArt {
  const width = layout.sizeCells[0] * PICTURE_UNITS_PER_CELL
  const height = layout.sizeCells[1] * PICTURE_UNITS_PER_CELL
  const anchors = layout.anchors.map((anchor) => {
    const { port } = anchor
    const name = escapeXml(port.name)
    const range = anchor.mV_min !== undefined || anchor.mV_max !== undefined ? ` · ${anchor.mV_min ?? 0}–${anchor.mV_max ?? '…'} mV` : ''
    const title = `${port.name} · ${port.kind}, ${FLOW_SHORT[flowOf({ kind: port.kind, dir: anchor.dir })]}${range}${port.note ? ` · ${port.note}` : ''}`
    const cx = anchor.x * width
    const cy = anchor.y * height
    const turn = anchor.angle ? ` transform="rotate(${anchor.angle} ${cx.toFixed(1)} ${cy.toFixed(1)})"` : ''
    return `<g id="pin-${name}" class="picture-anchor" data-port="${name}"><title>${escapeXml(title)}</title><circle id="pin-${name}-t" cx="${(anchor.x * width).toFixed(1)}" cy="${(anchor.y * height).toFixed(1)}" r="${ANCHOR_RADIUS}" fill="${KIND_COLOR[port.kind]}" fill-opacity="0.35" stroke="#fff" stroke-opacity="0.7" stroke-width="1"/><text class="picture-anchor-label"${turn} x="${(cx + ANCHOR_RADIUS + 1.5).toFixed(1)}" y="${(cy + LABEL_SIZE * 0.35).toFixed(1)}" font-size="${LABEL_SIZE}">${name}</text></g>`
  })
  return {
    width,
    height,
    markup: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}"><image href="${escapeXml(url)}" x="0" y="0" width="${width}" height="${height}" preserveAspectRatio="none"/>${anchors.join('')}</svg>`,
  }
}

const cache = new Map<string, readonly PictureVariant[]>()

/** The device's saved picture variants, in file-name order; a file that does not fit its pins csv is skipped. */
export function pictureVariants(type: Pick<DeviceType, 'id' | 'ports'>): readonly PictureVariant[] {
  let found = cache.get(type.id)
  if (!found) {
    const prefix = `/components/devices/${type.id}/`
    const name = /^app\/canvas\/([a-z][a-z0-9_]*)\/layout\.json$/
    found = Object.entries(layoutFiles)
      .flatMap(([path, raw]) => {
        const at = path.indexOf(prefix)
        const variant = at < 0 ? undefined : name.exec(path.slice(at + prefix.length))?.[1]
        if (!variant) return []
        const { layout } = resolveLayout(raw, type.ports)
        const url = layout && Object.entries(imageFiles).find(([imagePath]) => imagePath.endsWith(`${prefix}app/canvas/${variant}/${layout.image}`))?.[1]
        return layout && url ? [{ id: variant, label: layout.label ?? variant, layout, url }] : []
      })
      .sort((a, b) => a.id.localeCompare(b.id))
    cache.set(type.id, found)
  }
  return found
}

/** The variants a device can be drawn as: its generated symbols, then its pictures (a picture replaces a generated variant with the same ID). */
export function variantsOf(type: Pick<DeviceType, 'id' | 'ports' | 'symbolVariants'> | undefined): readonly SymbolVariant[] {
  if (!type) return []
  const pictures = pictureVariants(type)
  const asVariant = (picture: PictureVariant): SymbolVariant => ({ id: picture.id, label: picture.label, symbol: `${type.id}.${picture.id}.layout`, package: 'picture', hidden: picture.layout.unplaced, picture: true })
  const replaced = new Set(pictures.map((picture) => picture.id))
  return [...(type.symbolVariants ?? []).filter((variant) => !replaced.has(variant.id)), ...pictures.map(asVariant)]
}
