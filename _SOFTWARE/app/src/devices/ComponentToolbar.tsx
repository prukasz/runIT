import { useEffect, useRef, useState } from 'react'
import { ChevronDown, ImagePlus, RotateCw, Save, SlidersHorizontal, Tag, Wand2 } from 'lucide-react'
import { Button, ToggleChip } from '../components/Button'
import { NumberField, TextField } from '../components/FormField'
import { ListableField } from '../components/ListableField'
import { isRaster, NEW_VARIANT, SIZE_PERCENT, SNAP_CHOICES, ZOOMS, FIT } from './useComponentEditor'
import type { ComponentEditorApi, SnapChoice } from './useComponentEditor'
import './Devices.css'

/** How much detail the SVG trace keeps: the number of colours it reduces the picture to. */
const TRACE_DETAIL = [{ value: '8', label: 'Draft · 8 colours' }, { value: '16', label: 'Normal · 16 colours' }, { value: '32', label: 'Fine · 32 colours' }, { value: '64', label: 'Max · 64 colours' }]
const SNAP_LABELS: Record<SnapChoice, string> = { off: 'Snap off', pins: 'Snap to pins', '0.25': 'Grid 0.25 mm', '0.5': 'Grid 0.5 mm', '1': 'Grid 1 mm', '2.54': 'Grid 2.54 mm' }
const SNAP_OPTIONS = SNAP_CHOICES.map((choice) => ({ value: choice, label: SNAP_LABELS[choice] }))
const zoomLabel = (z: number): string => (z === FIT ? 'Fit' : `${Math.round(z * 100)}%`)
const MENU_WIDTH = 300

/** The picture's own tools: replace it, make white transparent, trace it to SVG, and the view zoom. */
function PictureMenu({ editor: e, raster, onClose }: { editor: ComponentEditorApi; raster: boolean; onClose: () => void }) {
  // A zoom from a gesture is not one of the presets: show it as the current value.
  const zoomOptions = [...(ZOOMS.includes(e.zoom) ? ZOOMS : [...ZOOMS, e.zoom].sort((a, b) => a - b))].map((z) => ({ value: String(z), label: zoomLabel(z) }))
  return (
    <div className="component-picture-menu" role="dialog" aria-label="Picture tools">
      <div className="component-picture-row">
        <Button size="sm" onClick={() => { e.fileInput.current?.click(); onClose() }}><ImagePlus aria-hidden="true" />Replace picture…</Button>
      </div>
      {raster && (
        <div className="component-picture-row">
          <ToggleChip selected={e.clearWhite} title="Make white transparent" onClick={() => void e.applyWhite(!e.clearWhite)}>White → transparent</ToggleChip>
          {e.clearWhite && <ToggleChip selected={e.outsideOnly} title="Only the white around the part, not white inside it (text, caps)" onClick={() => { e.setOutsideOnly(!e.outsideOnly); void e.applyWhite(true, e.tolerance, !e.outsideOnly) }}>Background only</ToggleChip>}
          {e.clearWhite && (
            <label className="component-picture-range" title={`Tolerance ${e.tolerance}`}>
              <span>Tolerance</span>
              <TextField type="range" min={0} max={120} value={e.tolerance} aria-label="White tolerance" onChange={(event) => e.setTolerance(Number(event.target.value))} onPointerUp={() => void e.applyWhite(true)} onKeyUp={() => void e.applyWhite(true)} />
            </label>
          )}
        </div>
      )}
      {(raster || e.picture?.raster) && (
        <div className="component-picture-row">
          <Button size="sm" disabled={e.tracing} title="Trace the picture to SVG" onClick={() => void e.convertToSvg()}><Wand2 aria-hidden="true" />{e.tracing ? 'Tracing…' : raster ? 'Convert to SVG' : 'Re-trace'}</Button>
          <ListableField className="component-picture-trace" aria-label="Trace detail" title="Trace detail: the colours the picture is reduced to" value={String(e.trace.colors)} options={TRACE_DETAIL} onChange={(value) => e.setTrace({ ...e.trace, colors: Number(value) })} />
          {!raster && <Button size="sm" onClick={() => void e.backToRaster()}>Back to PNG</Button>}
        </div>
      )}
      <div className="component-picture-row">
        <span className="component-picture-label">Zoom</span>
        <ListableField className="component-picture-zoom" aria-label="Zoom" title="Zoom (the wheel and a pinch zoom too)" value={String(e.zoom)} options={zoomOptions} onChange={(value) => e.setZoom(Number(value))} />
      </div>
    </div>
  )
}

/*
 * The anchor editor's controls in the app's top toolbar: device and variant, Save, the picture menu (replace, white to
 * transparent, trace to SVG, zoom), size, snap, labels and the name angle. Built from the shared controls
 * (ListableField, TextField, Button, ToggleChip); the pins are in the right panel (`ComponentPins`).
 */
export function ComponentToolbar({ editor: e }: { editor: ComponentEditorApi }) {
  const raster = isRaster(e.picture)
  const placedSelection = !!e.selected && !!e.anchors[e.selected]
  const deviceOptions = e.types.map((type) => ({ value: type.id, label: type.title }))
  const variantOptions = [{ value: NEW_VARIANT, label: 'New variant…' }, ...e.variants.map((name) => ({ value: name, label: name }))]

  const [menu, setMenu] = useState<{ top: number; left: number }>()
  const menuButton = useRef<HTMLButtonElement>(null)
  const menuPanel = useRef<HTMLDivElement>(null)
  const closeMenu = () => setMenu(undefined)

  // The menu floats under its button (fixed, so the scrolling toolbar does not clip it) and closes on a press outside or Escape.
  useEffect(() => {
    if (!menu) return
    const away = (event: PointerEvent) => {
      const target = event.target as Node
      if (!menuPanel.current?.contains(target) && !menuButton.current?.contains(target)) closeMenu()
    }
    const escape = (event: KeyboardEvent) => { if (event.key === 'Escape') closeMenu() }
    window.addEventListener('pointerdown', away)
    window.addEventListener('keydown', escape)
    return () => {
      window.removeEventListener('pointerdown', away)
      window.removeEventListener('keydown', escape)
    }
  }, [menu])

  const pictureButton = () => {
    if (!e.picture) return e.fileInput.current?.click()
    const rect = menuButton.current?.getBoundingClientRect()
    if (menu || !rect) return closeMenu()
    setMenu({ top: rect.bottom + 6, left: Math.max(8, Math.min(rect.left, window.innerWidth - MENU_WIDTH - 8)) })
  }

  return (
    <div className="component-toolbar" role="group" aria-label="Component editor" onWheel={(event) => { const bar = event.currentTarget; if (bar.scrollWidth > bar.clientWidth && Math.abs(event.deltaY) > Math.abs(event.deltaX)) bar.scrollLeft += event.deltaY }}>
      <ListableField className="component-toolbar-device" aria-label="Device" title="Device" value={e.deviceId} options={deviceOptions} onChange={e.setDeviceId} />
      <ListableField className="component-toolbar-variant" aria-label="Variant" title="Variant" value={e.variant} options={variantOptions} onChange={(name) => void e.openVariant(name)} />
      <Button variant="primary" size="sm" className="component-toolbar-save" disabled={!!e.saveBlocker} title={e.saveBlocker ?? 'Save the picture and layout'} onClick={() => void e.save()}><Save aria-hidden="true" />Save</Button>

      <span className="component-toolbar-sep" />
      <TextField ref={e.fileInput} type="file" accept=".svg,.png,.jpg,.jpeg,.webp,image/*" hidden onChange={(event) => { void e.pickFile(event.target.files?.[0]); event.target.value = '' }} />
      <Button ref={menuButton} size="sm" className="component-toolbar-picture" aria-haspopup={e.picture ? 'dialog' : undefined} aria-expanded={e.picture ? !!menu : undefined} title={e.picture ? 'Picture tools: replace, white to transparent, trace to SVG, zoom' : 'Choose a picture of the IC or board (SVG or PNG)'} onClick={pictureButton}>
        {e.picture ? <SlidersHorizontal aria-hidden="true" /> : <ImagePlus aria-hidden="true" />}{e.picture ? 'Picture' : 'Choose picture'}{e.picture ? <ChevronDown aria-hidden="true" /> : null}
      </Button>
      {menu && e.picture && (
        <div ref={menuPanel} className="component-picture-menu-anchor" style={{ top: menu.top, left: menu.left, width: MENU_WIDTH }}>
          <PictureMenu editor={e} raster={raster} onClose={closeMenu} />
        </div>
      )}

      <span className="component-toolbar-sep" />
      <label className="component-toolbar-size" title="Picture size: percent of its starting size, in 5% steps">
        <TextField type="range" min={SIZE_PERCENT.min} max={SIZE_PERCENT.max} step={SIZE_PERCENT.step} value={Math.min(SIZE_PERCENT.max, Math.max(SIZE_PERCENT.min, e.sizePercent))} disabled={!e.picture} onChange={(event) => e.setSizePercent(Number(event.target.value))} />
        <span className="component-toolbar-value">{e.sizePercent}%</span>
      </label>
      <label className="component-toolbar-width" title="Width in millimetres (one cell is one millimetre on the board canvas)">
        <NumberField min={1} max={240} value={e.cellsWide} disabled={!e.picture} aria-label="Width in millimetres" onCommit={e.setCellsWide} />
        <span>mm</span>
      </label>

      <ListableField className="component-toolbar-snap" aria-label="Snap" title="Snap while placing a pin: line up with the other pins (same row or column, same pitch), or a grid in millimetres" value={e.snap} options={SNAP_OPTIONS} onChange={(value) => e.setSnap(value as SnapChoice)} />
      <ToggleChip selected={e.labels} aria-label="Pin labels" title={e.labels ? 'Hide the pin names on the picture' : 'Show the pin names on the picture'} onClick={() => e.setLabels(!e.labels)}><Tag aria-hidden="true" size={13} /></ToggleChip>
      <Button size="sm" disabled={!placedSelection} title={placedSelection ? `Turn the name of ${e.selected} a quarter turn (R)` : 'Select a placed pin to turn its name'} onClick={() => e.selected && e.rotateLabel(e.selected)}><RotateCw aria-hidden="true" />Name{placedSelection ? ` ${e.angleOf(e.selected as string)}°` : ''}</Button>
    </div>
  )
}
