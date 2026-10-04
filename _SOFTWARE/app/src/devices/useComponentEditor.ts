import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { PointerEvent as ReactPointerEvent } from 'react'
import { GRID } from '../domain/canvas'
import type { DeviceCatalog } from '../domain/descriptors'
import { cellsFor, layoutOf, resolveLayout } from '../domain/devices/componentLayout'
import { nextLabelAngle } from '../domain/devices/componentLayout'
import type { Anchor, ComponentLayout } from '../domain/devices/componentLayout'
import { whiteToTransparent } from '../domain/devices/knockout'
import { readStored, usePersistedChoice, writeStored } from '../hooks/useStorage'
import { snapAnchor } from '../domain/devices/anchorSnap'
import type { SnapMode } from '../domain/devices/anchorSnap'
import { imageUrl, listComponentFiles, loadLayout, saveVariant } from './componentFiles'
import { DEFAULT_TRACE, traceToSvg } from './traceSvg'
import type { TraceSettings } from './traceSvg'

/*
 * State and actions of the anchor editor (board-canvas-concept.md §4a), shared by its three parts: the top toolbar
 * (device, variant, picture, size, save), the stage (the picture with its anchors) and the right panel (the pins).
 * It works on one component variant: pick a device and a variant, give it a picture (SVG / PNG), place the csv pins
 * on it, and Save writes the layout and the picture into the device folder through the dev server.
 */

/** 0 = fit the picture to the stage. */
export const FIT = 0
export const ZOOMS = [FIT, 0.5, 1, 2, 3, 4, 6, 8]
export const MIN_ZOOM = 0.25
/** Snap choices of the toolbar: free, line up with the other pins, or a grid of this many millimetres. */
export const SNAP_CHOICES = ['off', 'pins', '0.25', '0.5', '1', '2.54'] as const
export type SnapChoice = (typeof SNAP_CHOICES)[number]
export const snapModeOf = (choice: SnapChoice): SnapMode => (choice === 'off' || choice === 'pins' ? choice : Number(choice))
/** How close (screen pixels) a pin must come to another's row or column to snap to it. */
const SNAP_REACH_PX = 8
export const MAX_ZOOM = 16

/** The point of the picture a zoom keeps under a screen point: fractions of the picture and the screen position. */
export interface ZoomAnchor { readonly fx: number; readonly fy: number; readonly clientX: number; readonly clientY: number }
/** An anchor is 0.6 cell across (as on the board canvas), so it keeps its size relative to the picture at any zoom. */
export const ANCHOR_CELLS = 0.6
export const NEW_VARIANT = ''
/** Size slider: percent of the size the picture had when it was chosen or opened. */
export const SIZE_PERCENT = { min: 10, max: 400, step: 5 } as const
/** Saving a picture reloads the dev page; this brings the editor back to the variant just saved. */
const REOPEN_KEY = 'runit.component.reopen'
interface Reopen { readonly deviceId: string; readonly variant: string }
const NUDGE = 0.002

const clamp01 = (value: number): number => Math.min(1, Math.max(0, value))

export interface Picture {
  /** Path in the device folder once saved. */
  readonly saved?: string
  /** A picture picked in this session, not yet saved. */
  readonly file?: File
  readonly url: string
  readonly width: number
  readonly height: number
  /** The picture as chosen / loaded, kept while white is made transparent so it can be undone. */
  readonly original?: Blob
  /** After "Convert to SVG": the raster that was traced (white choice applied), for re-tracing and going back. */
  readonly raster?: Blob
}

export const isRaster = (picture: Picture | undefined): boolean => !!picture && !/\.svg(\?|$)/i.test(picture.file?.name ?? picture.saved ?? '')

/** The picture's pixels with white made transparent, as a PNG. */
async function knockedOut(source: Blob, tolerance: number, outsideOnly: boolean): Promise<Blob> {
  const bitmap = await createImageBitmap(source)
  const canvas = document.createElement('canvas')
  canvas.width = bitmap.width
  canvas.height = bitmap.height
  const context = canvas.getContext('2d')
  if (!context) throw new Error('The picture could not be processed.')
  context.drawImage(bitmap, 0, 0)
  const pixels = context.getImageData(0, 0, canvas.width, canvas.height)
  whiteToTransparent(pixels.data, canvas.width, canvas.height, tolerance, outsideOnly)
  context.putImageData(pixels, 0, 0)
  return new Promise((resolve, reject) => canvas.toBlob((blob) => (blob ? resolve(blob) : reject(new Error('The picture could not be processed.'))), 'image/png'))
}

/** A variant name from a picture's file name: lower case, `a-z0-9_`, starting with a letter. */
const variantFromFile = (fileName: string): string => {
  const stem = fileName.replace(/\.[^.]+$/, '').toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^[^a-z]+/, '').replace(/_+$/, '')
  return stem || 'variant'
}

export interface EditorStatus { readonly text: string; readonly error?: boolean }

/** `active`: the editor is on screen; the dev server's files are listed only then. */
export function useComponentEditor(catalog: DeviceCatalog, active: boolean) {
  const types = useMemo(() => catalog.types.filter((type) => type.ports.length > 0), [catalog])
  const reopenRef = useRef<Reopen | undefined>(readStored<Reopen | undefined>(REOPEN_KEY, (raw) => JSON.parse(raw) as Reopen, undefined))
  const [deviceId, setDeviceId] = useState(reopenRef.current?.deviceId ?? types[0]?.id ?? '')
  const ports = useMemo(() => catalog.type(deviceId)?.ports ?? [], [catalog, deviceId])
  const [variants, setVariants] = useState<readonly string[]>([])
  const [variant, setVariant] = useState(NEW_VARIANT)
  /** The variant's name as typed: a new variant's, or the name an opened one will be saved under (a rename). */
  const [nameDraft, setNameDraft] = useState('')
  /** The name shown for the variant in the palette (the layout's `label`); the name when empty. */
  const [variantLabel, setVariantLabelState] = useState('')
  const [picture, setPicture] = useState<Picture>()
  const [cellsWide, setCellsWideState] = useState(10)
  /** The width (cells) the size slider's 100% stands for. */
  const [baseCells, setBaseCells] = useState(10)
  const [anchors, setAnchors] = useState<Record<string, Anchor>>({})
  const [selected, setSelected] = useState<string>()
  const [zoom, setZoom] = useState(1)
  /** Counts "back to 100% and centred" requests, so the stage re-centres even when the zoom is already 100%. */
  const [viewResets, setViewResets] = useState(0)
  const [stageWidth, setStageWidth] = useState(0)
  const [status, setStatus] = useState<EditorStatus>()
  const [dirty, setDirty] = useState(false)
  const [clearWhite, setClearWhite] = useState(false)
  const [tolerance, setTolerance] = useState(40)
  const [outsideOnly, setOutsideOnly] = useState(true)
  const [trace, setTrace] = useState<TraceSettings>(DEFAULT_TRACE)
  const [tracing, setTracing] = useState(false)
  const [labels, setLabels] = useState(true)
  const [snap, setSnap] = usePersistedChoice<SnapChoice>('runit.component.snap', SNAP_CHOICES, 'pins')
  /** Rows and columns the pin being placed has lined up with, as fractions of the picture (guide lines). */
  const [guides, setGuides] = useState<{ x?: number; y?: number }>()
  const frameRef = useRef<HTMLDivElement>(null)
  const dragging = useRef<string | undefined>(undefined)
  const fileInput = useRef<HTMLInputElement>(null)

  const variantName = nameDraft.trim().toLowerCase()
  const nameOk = /^[a-z][a-z0-9_]*$/.test(variantName)
  /** An opened variant saved under another name: its files are renamed. */
  const isRename = variant !== NEW_VARIANT && variantName !== variant
  const nameTaken = variantName !== variant && variants.includes(variantName)
  const sizeCells = picture ? cellsFor(cellsWide, picture.width, picture.height) : [cellsWide, cellsWide]
  // On screen pixels per canvas pixel: the chosen zoom, or whatever fits the picture's width in the stage.
  const scale = zoom === FIT ? Math.min(8, Math.max(MIN_ZOOM, (stageWidth - 28) / (sizeCells[0] * GRID))) : zoom
  const scaleRef = useRef(scale)
  scaleRef.current = scale
  const zoomAnchor = useRef<ZoomAnchor | undefined>(undefined)
  const sizePercent = Math.round(((cellsWide / baseCells) * 100) / SIZE_PERCENT.step) * SIZE_PERCENT.step

  /** Zooms by a factor about a screen point: that point of the picture stays under it (the stage applies the scroll). */
  const zoomBy = (factor: number, at: { clientX: number; clientY: number }) => {
    const next = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, scaleRef.current * factor))
    const rect = frameRef.current?.getBoundingClientRect()
    if (rect && rect.width && rect.height) zoomAnchor.current = { fx: (at.clientX - rect.left) / rect.width, fy: (at.clientY - rect.top) / rect.height, clientX: at.clientX, clientY: at.clientY }
    scaleRef.current = next
    setZoom(next)
  }

  /** The picture at 100%, centred in the stage. */
  const resetView = () => {
    zoomAnchor.current = undefined
    scaleRef.current = 1
    setZoom(1)
    setViewResets((count) => count + 1)
  }

  const fail = (error: unknown) => setStatus({ text: error instanceof Error ? error.message : String(error), error: true })

  const setCellsWide = (cells: number) => {
    setCellsWideState(Math.min(240, Math.max(1, Math.round(cells) || 1)))
    setDirty(true)
  }
  const setSizePercent = (percent: number) => setCellsWide((baseCells * percent) / 100)

  const refreshVariants = useCallback(async (id: string) => {
    try {
      setVariants((await listComponentFiles(id)).variants)
    } catch {
      setVariants([])
      setStatus({ text: 'The component files are only reachable while the dev server runs.', error: true })
    }
  }, [])

  const measure = (url: string, extra: Pick<Picture, 'saved' | 'file'>): Promise<Picture> =>
    new Promise((resolve, reject) => {
      const image = new Image()
      image.onload = () => resolve({ ...extra, url, width: image.naturalWidth || GRID * 10, height: image.naturalHeight || GRID * 5 })
      image.onerror = () => reject(new Error('The picture could not be read.'))
      image.src = url
    })

  const openVariant = async (name: string) => {
    setVariant(name)
    setNameDraft(name)
    setVariantLabelState('')
    resetView()
    setClearWhite(false)
    setSelected(undefined)
    setDirty(false)
    setStatus(undefined)
    if (name === NEW_VARIANT) {
      setPicture(undefined)
      setAnchors({})
      return
    }
    try {
      const { layout, problems } = await loadLayout(deviceId, name, ports)
      if (!layout) return fail(new Error(problems.join(' ')))
      const editable = layoutOf(layout)
      setAnchors({ ...editable.anchors })
      setVariantLabelState(layout.label ?? '')
      setPicture(await measure(`${imageUrl(deviceId, name, layout.image)}?t=${Date.now()}`, { saved: layout.image }))
      setCellsWideState(layout.sizeCells[0])
      setBaseCells(layout.sizeCells[0])
      if (problems.length) setStatus({ text: problems.join(' '), error: true })
    } catch (error) {
      fail(error)
    }
  }

  // A device change: reset the editor. Not on startup: the editor may be reopening the variant just saved, and React's
  // development re-run of effects would otherwise clear the variant while its files are still loading.
  const lastDevice = useRef(deviceId)
  useEffect(() => {
    if (lastDevice.current === deviceId) return
    lastDevice.current = deviceId
    setVariant(NEW_VARIANT)
    setNameDraft('')
    setVariantLabelState('')
    setPicture(undefined)
    setClearWhite(false)
    setAnchors({})
    setSelected(undefined)
    setDirty(false)
    setStatus(undefined)
  }, [deviceId])

  // On screen: list the device's saved variants, and come back to the one just saved (a save reloads the dev page).
  useEffect(() => {
    if (!active || !deviceId) return
    void refreshVariants(deviceId)
    const reopen = reopenRef.current
    reopenRef.current = undefined
    if (!reopen) return
    writeStored(REOPEN_KEY, '')
    if (reopen.deviceId === deviceId) void openVariant(reopen.variant).then(() => setStatus((current) => current ?? { text: `Saved ${reopen.variant}.` }))
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active, deviceId, refreshVariants])

  const pickFile = async (file: File | undefined) => {
    if (!file) return
    try {
      const next = { ...(await measure(URL.createObjectURL(file), { file })), original: file }
      setClearWhite(false)
      if (variant === NEW_VARIANT && !nameDraft.trim()) setNameDraft(variantFromFile(file.name))
      setPicture(next)
      resetView()
      // About two pixels per millimetre to start with; the size controls set the real size.
      const cells = Math.min(60, Math.max(4, Math.round(next.width / (GRID * 2))))
      setCellsWideState(cells)
      setBaseCells(cells)
      setDirty(true)
    } catch (error) {
      fail(error)
    }
  }

  /** Shows and saves the picture with white transparent (`on`), or as it was chosen / loaded. */
  const applyWhite = async (on: boolean, level: number = tolerance, outside: boolean = outsideOnly) => {
    if (!picture || !isRaster(picture)) return
    setClearWhite(on)
    try {
      const original = picture.original ?? (await (await fetch(picture.url)).blob())
      const base = picture.file?.name.replace(/\.[^.]+$/, '') ?? variantName
      const blob = on ? await knockedOut(original, level, outside) : original
      const file = new File([blob], on ? `${base}.png` : (original instanceof File ? original.name : `${base}.png`), { type: blob.type })
      setPicture({ ...(await measure(URL.createObjectURL(blob), { file })), original })
      setDirty(true)
    } catch (error) {
      setClearWhite(!on)
      fail(error)
    }
  }

  /** Replaces the raster picture by a colour trace of it (the same pixel size, so the grid width stays), or re-traces. */
  const convertToSvg = async (settings: TraceSettings = trace) => {
    const source = isRaster(picture) ? undefined : picture?.raster
    if (!picture || (!isRaster(picture) && !source)) return
    setTracing(true)
    try {
      // Trace what is on screen, so a "White -> transparent" result is kept.
      const shown = source ?? (await (await fetch(picture.url)).blob())
      const svg = await traceToSvg(shown, settings)
      const base = (picture.file?.name ?? picture.saved ?? variantName).replace(/^.*\//, '').replace(/\.[^.]+$/, '')
      const blob = new Blob([svg], { type: 'image/svg+xml' })
      setPicture({ ...(await measure(URL.createObjectURL(blob), { file: new File([blob], `${base}.svg`, { type: blob.type }) })), original: picture.original ?? shown, raster: shown })
      setDirty(true)
      setStatus({ text: `Traced to SVG (${Math.round(blob.size / 1024)} KB, ${settings.colors} colours).` })
    } catch (error) {
      fail(error)
    } finally {
      setTracing(false)
    }
  }

  /** Back from a trace to the picture as chosen / loaded. */
  const backToRaster = async () => {
    const original = picture?.raster ?? picture?.original
    if (!picture || !original) return
    const base = (picture.file?.name ?? variantName).replace(/\.[^.]+$/, '')
    const file = new File([original], original instanceof File ? original.name : `${base}.png`, { type: original.type })
    setPicture({ ...(await measure(URL.createObjectURL(original), { file })), original: picture.original ?? original })
    setDirty(true)
  }

  const place = (name: string, x: number, y: number) => {
    setAnchors((current) => ({ ...current, [name]: { ...current[name], x: clamp01(x), y: clamp01(y) } }))
    setDirty(true)
  }

  const fractionAt = (event: { clientX: number; clientY: number }): { x: number; y: number } | undefined => {
    const rect = frameRef.current?.getBoundingClientRect()
    return rect ? { x: (event.clientX - rect.left) / rect.width, y: (event.clientY - rect.top) / rect.height } : undefined
  }

  /** The point after the toolbar's snap: lined up with the other placed pins, or on the grid. */
  const snapped = (name: string, at: { x: number; y: number }) => {
    const others = Object.entries(anchors).filter(([other]) => other !== name).map(([, anchor]) => anchor)
    const result = snapAnchor(at, others, {
      mode: snapModeOf(snap),
      thresholdX: SNAP_REACH_PX / (sizeCells[0] * GRID * scale),
      thresholdY: SNAP_REACH_PX / (sizeCells[1] * GRID * scale),
      sizeCells: [sizeCells[0], sizeCells[1]],
    })
    setGuides(result.guideX !== undefined || result.guideY !== undefined ? { x: result.guideX, y: result.guideY } : undefined)
    return result
  }

  const onFrameClick = (event: React.MouseEvent) => {
    if (!selected || anchors[selected] || dragging.current) return
    const at = fractionAt(event)
    if (at) {
      const target = snapped(selected, at)
      place(selected, target.x, target.y)
    }
    setGuides(undefined)
  }

  const onAnchorDown = (name: string, event: ReactPointerEvent) => {
    event.stopPropagation()
    setSelected(name)
    dragging.current = name
    event.currentTarget.setPointerCapture(event.pointerId)
  }
  const onAnchorMove = (name: string, event: ReactPointerEvent) => {
    if (dragging.current !== name) return
    const at = fractionAt(event)
    if (at) {
      const target = snapped(name, at)
      place(name, target.x, target.y)
    }
  }
  const onAnchorUp = () => {
    setGuides(undefined)
    window.setTimeout(() => { dragging.current = undefined }, 0)
  }

  const update = (name: string, change: Partial<Anchor> | { [K in keyof Anchor]?: undefined }) => {
    setAnchors((current) => {
      const merged = { ...current[name], ...change } as Record<string, unknown>
      for (const key of Object.keys(merged)) if (merged[key] === undefined) delete merged[key]
      return { ...current, [name]: merged as unknown as Anchor }
    })
    setDirty(true)
  }
  const unplace = (name: string) => {
    setAnchors((current) => {
      const { [name]: _removed, ...rest } = current
      return rest
    })
    setDirty(true)
  }

  /** Arrow keys nudge the selected anchor (Shift: bigger steps), Delete takes it off, R turns its name; not while typing in a field. */
  const onKeyDown = (event: KeyboardEvent) => {
    const tag = (event.target as HTMLElement | null)?.tagName
    if (!selected || !anchors[selected] || tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return
    const step = event.shiftKey ? NUDGE * 5 : NUDGE
    const move: Record<string, [number, number]> = { ArrowLeft: [-step, 0], ArrowRight: [step, 0], ArrowUp: [0, -step], ArrowDown: [0, step] }
    if (move[event.key]) {
      event.preventDefault()
      place(selected, anchors[selected].x + move[event.key][0], anchors[selected].y + move[event.key][1])
    } else if (event.key === 'Delete' || event.key === 'Backspace') {
      unplace(selected)
    } else if ((event.key === 'r' || event.key === 'R') && !event.ctrlKey && !event.metaKey) {
      rotateLabel(selected)
    }
  }

  // Keys work anywhere in the editor, not only over the picture: the latest handler sits behind a ref.
  const keyHandler = useRef(onKeyDown)
  keyHandler.current = onKeyDown
  useEffect(() => {
    if (!active) return
    const listener = (event: KeyboardEvent) => keyHandler.current(event)
    window.addEventListener('keydown', listener)
    return () => window.removeEventListener('keydown', listener)
  }, [active])

  const saveBlocker = !picture ? 'Choose a picture first' : !nameOk ? 'Name the variant in the right panel: lower case letters, digits and _, starting with a letter' : nameTaken ? `A variant named ${variantName} already exists` : !dirty ? 'Nothing changed' : undefined

  const save = async () => {
    if (!picture || !nameOk) return
    try {
      // The picture always goes with the layout into canvas/<variant>/: a new file, or the saved one copied on a rename.
      let upload: { name: string; data: Blob }
      if (picture.file) {
        const ext = /\.(svg|png|jpe?g|webp)$/i.exec(picture.file.name)?.[1].toLowerCase() ?? 'png'
        upload = { name: `${variantName}.${ext}`, data: picture.file }
      } else if (picture.saved) {
        upload = { name: picture.saved, data: await (await fetch(picture.url)).blob() }
      } else return
      const image = upload.name
      const layout: ComponentLayout = { image, size_cells: [sizeCells[0], sizeCells[1]], ...(variantLabel.trim() ? { label: variantLabel.trim() } : {}), anchors }
      const checked = resolveLayout(JSON.parse(JSON.stringify(layout)), ports)
      if (!checked.layout) return fail(new Error(checked.problems.join(' ')))
      // The dev page reloads when a bundled file changes: note where to come back to before the files land.
      writeStored(REOPEN_KEY, JSON.stringify({ deviceId, variant: variantName }))
      await saveVariant(deviceId, variantName, layout, ports, upload, isRename && picture.saved ? { from: variant, image: picture.saved } : undefined)
      setPicture({ ...picture, saved: image, file: undefined })
      setVariant(variantName)
      setDirty(false)
      setBaseCells(cellsWide)
      setStatus({ text: `${isRename ? `Renamed ${variant} to ${variantName} and saved` : `Saved ${variantName}`} (${Object.keys(anchors).length} of ${ports.length} pins placed, ${sizeCells[0]} × ${sizeCells[1]} mm).` })
      await refreshVariants(deviceId)
    } catch (error) {
      writeStored(REOPEN_KEY, '')
      fail(error)
    }
  }

  const angleOf = (name: string): number => anchors[name]?.angle ?? 0
  /** Turns one pin's name a quarter turn. */
  const rotateLabel = (name: string) => {
    if (anchors[name]) update(name, { angle: nextLabelAngle(angleOf(name)) || undefined })
  }

  const placed = Object.keys(anchors).filter((name) => ports.some((port) => port.name === name)).length

  return {
    types, deviceId, setDeviceId, ports, variants, variant, openVariant, nameDraft, setNameDraft: (name: string) => { setNameDraft(name); setDirty(true) }, nameOk, nameTaken, isRename,
    variantLabel, setVariantLabel: (label: string) => { setVariantLabelState(label); setDirty(true) },
    picture, pickFile, fileInput, frameRef,
    cellsWide, setCellsWide, sizeCells, sizePercent, setSizePercent,
    zoom, setZoom, scale, zoomBy, zoomAnchor, resetView, viewResets, stageWidth, setStageWidth,
    anchors, selected, setSelected, placed, place, update, unplace,
    clearWhite, tolerance, setTolerance, outsideOnly, setOutsideOnly, applyWhite,
    trace, setTrace, tracing, convertToSvg, backToRaster,
    labels, setLabels, angleOf, rotateLabel, snap, setSnap, guides, gridStepMm: snap === 'off' || snap === 'pins' ? 5 : Number(snap),
    status, dirty, saveBlocker, save,
    onFrameClick, onAnchorDown, onAnchorMove, onAnchorUp,
  }
}

export type ComponentEditorApi = ReturnType<typeof useComponentEditor>
