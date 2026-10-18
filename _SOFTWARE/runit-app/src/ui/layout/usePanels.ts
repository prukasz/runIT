import { useEffect, useState } from 'react'
import { readStored, writeStored } from '../hooks/useStorage'
import { clampWidth, COLLAPSED_WIDTH, FRAME_GAP, MAX_RIGHT_PANEL, viewWidth } from './panelWidth'

/** A remembered panel width. */
function usePanelWidth(key: string, fallback: number) {
  const [width, setWidth] = useState(() => readStored(key, (raw) => (Number.isFinite(Number(raw)) ? Number(raw) : undefined), fallback))
  return [width, (next: number) => { setWidth(next); writeStored(key, String(next)) }] as const
}

/** The window's width, kept current: the panels are clamped against it. */
function useViewWidth(): number {
  const [width, setWidth] = useState(viewWidth)
  useEffect(() => {
    const onResize = () => setWidth(viewWidth())
    window.addEventListener('resize', onResize)
    return () => window.removeEventListener('resize', onResize)
  }, [])
  return width
}

/**
 * The two side panels of the frame: open or collapsed, how wide, being dragged. The left one is the explorer (open at first), the
 * right one the details (collapsed at first). Widths are remembered; each panel leaves the centre its minimum.
 */
export function usePanels() {
  const view = useViewWidth()
  const [leftOpen, setLeftOpen] = useState(true)
  const [rightOpen, setRightOpen] = useState(false)
  const [leftWidth, setLeftWidth] = usePanelWidth('runit.leftWidth', 260)
  const [rightWidth, setRightWidth] = usePanelWidth('runit.rightWidth', 440)
  const [resizing, setResizing] = useState(false)

  const left = clampWidth(leftWidth, rightOpen ? rightWidth : COLLAPSED_WIDTH, view)
  const right = clampWidth(rightWidth, leftOpen ? leftWidth : COLLAPSED_WIDTH, view, MAX_RIGHT_PANEL)

  return {
    leftOpen,
    rightOpen,
    left,
    right,
    resizing,
    setLeftOpen,
    setRightOpen,
    setResizing,
    /** Dragging the left edge to `x`. */
    dragLeft: (x: number) => setLeftWidth(clampWidth(x - FRAME_GAP, rightOpen ? right : COLLAPSED_WIDTH, view)),
    /** Dragging the right edge to `x` (a position in the window; the width is what is left of it, short of the frame's edge). */
    dragRight: (x: number) => setRightWidth(clampWidth(view - x - FRAME_GAP, leftOpen ? left : COLLAPSED_WIDTH, view, MAX_RIGHT_PANEL)),
    /** Opens the right panel as wide as it may be (the connection panel wants room). */
    widenRight: () => setRightWidth(clampWidth(MAX_RIGHT_PANEL, leftOpen ? left : COLLAPSED_WIDTH, view, MAX_RIGHT_PANEL)),
  }
}
