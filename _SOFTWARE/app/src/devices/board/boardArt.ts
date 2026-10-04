import { GRID } from '../../domain/canvas'
import type { Viewport } from '../../domain/canvas'

/*
 * Board art: an SVG drawn in screen units whose `<g id>` groups are the board's terminals. The art's own `<style>`
 * is global in a page, so every rule is scoped under a class before the markup is inlined.
 */

/** One grid cell is one millimetre; the art is drawn at 20 units to the millimetre, so it is shown at this scale. */
export const ART_UNITS_PER_MM = 20
export const ART_SCALE = GRID / ART_UNITS_PER_MM

export interface BoardArt {
  /** The markup with its style scoped, ready to inline. */
  markup: string
  width: number
  height: number
}

export interface BoardTerminal {
  id: string
  title: string
}

/** Prefix every selector of the art's `<style>` rules (one rule per line, starting with `.`) with `.scope`. */
export function scopeSvg(raw: string, scope: string): BoardArt {
  const box = /viewBox="\s*[-\d.]+\s+[-\d.]+\s+([\d.]+)\s+([\d.]+)\s*"/.exec(raw)
  if (!box) throw new Error('SVG has no viewBox')
  const markup = raw.replace(/<style>([\s\S]*?)<\/style>/, (_, css: string) =>
    `<style>${css.replace(/^(\s*)(\.[^{\n]*?)(\s*\{)/gm, (_rule, indent: string, selectors: string, open: string) => `${indent}${selectors.split(',').map((selector) => `.${scope} ${selector.trim()}`).join(', ')}${open}`)}</style>`,
  )
  return { markup, width: Number(box[1]), height: Number(box[2]) }
}

/** The terminal group (a `<g id>` below the svg root) under an event target, with its `<title>`. */
export function terminalAt(target: EventTarget | null, root: Element | null): BoardTerminal | undefined {
  if (!(target instanceof Element) || !root) return undefined
  let group = target.closest('g[id]')
  // The outermost id'd group inside the art is the terminal (reg blocks and the ESP module are groups too).
  while (group && group.parentElement?.closest('g[id]') && root.contains(group.parentElement.closest('g[id]'))) group = group.parentElement.closest('g[id]')
  if (!group || !root.contains(group)) return undefined
  return { id: group.id, title: group.querySelector(':scope > title')?.textContent ?? '' }
}

/** Centre `art` in a surface of `size`, as large as fits with a margin (never above 1). */
export function fitViewport(art: { width: number; height: number }, size: { width: number; height: number }, margin = 24): Viewport {
  const zoom = Math.min(1, Math.max(0.05, Math.min((size.width - 2 * margin) / art.width, (size.height - 2 * margin) / art.height)))
  return { zoom, x: (size.width - art.width * zoom) / 2, y: (size.height - art.height * zoom) / 2 }
}
