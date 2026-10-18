/* The widths of the two side panels. A panel is never narrower than MIN_PANEL, never wider than its maximum, and leaves the centre
 * column MIN_CENTER wide; a collapsed panel is the icon bar (COLLAPSED_WIDTH). Pure, so a test can hold the rules. */

/** A bar button (Layout.css `--bar-button`, 44) with `--bar-inset` (8) on both sides and the 1px borders. */
export const COLLAPSED_WIDTH = 62
export const MIN_PANEL = 200
export const MAX_PANEL = 500
export const MAX_RIGHT_PANEL = 520
export const MIN_CENTER = 420
/** The frame floats its columns as cards: this much of the window's edge and between two columns (Layout.css `--frame-gap`). */
export const FRAME_GAP = 6
/** What the gaps and the edges take from the window: a left edge, two gaps between three columns, a right edge. */
export const FRAME_SPACE = 4 * FRAME_GAP
/** One arrow key on a resize handle moves the edge this far. */
export const KEY_STEP = 16

/**
 * The width a panel may have: `width` wanted, `other` the width of the other panel, `view` the width of the window, `max` the
 * panel's own ceiling. The window decides first (the centre keeps MIN_CENTER), then the ceiling, then the floor.
 */
export const clampWidth = (width: number, other: number, view: number, max = MAX_PANEL): number =>
  Math.round(Math.min(Math.max(MIN_PANEL, view - other - MIN_CENTER - FRAME_SPACE), max, Math.max(MIN_PANEL, width)))

/** The width of the window; a default where there is none (a test, a server). */
export const viewWidth = (): number => (typeof window === 'undefined' ? 1920 : window.innerWidth)
