import { useEffect, useState } from 'react'
import { readStored, writeStored } from '@ui/hooks/useStorage'
import { desktop } from '../desktop'

const KEY = 'runit.app.zoom'

/**
 * The desktop app's interface scale (1 = 100 %): the window's, remembered and set again at every start; the keyboard
 * (Ctrl + / Ctrl − / Ctrl 0) changes it too. `undefined` in a browser, which has its own zoom.
 */
export function useAppZoom(): { zoom: number | undefined; setZoom: (factor: number) => void } {
  const [zoom, setZoomState] = useState<number | undefined>(undefined)
  useEffect(() => {
    if (!desktop) return
    const remembered = readStored(KEY, (raw) => { const n = Number(raw); return n >= 0.5 && n <= 3 ? n : undefined }, undefined)
    if (remembered !== undefined) desktop.setZoom(remembered)
    else void desktop.getZoom().then(setZoomState)
    return desktop.onZoom((factor) => { setZoomState(factor); writeStored(KEY, String(factor)) })
  }, [])
  return { zoom, setZoom: (factor) => desktop?.setZoom(factor) }
}
