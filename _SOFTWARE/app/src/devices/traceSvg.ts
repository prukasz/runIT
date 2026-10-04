/*
 * Raster picture -> SVG by colour tracing (ImageTracer, loaded on first use). Flat art traces well; a shaded render is
 * simplified to `colors` flat colours. Transparent pixels (from "White -> transparent") stay transparent: the tracer
 * does not handle alpha, so they are traced as a key colour whose paths are dropped afterwards.
 */

export interface TraceSettings {
  /** Colours in the result (more = more detail, bigger file). */
  readonly colors: number
  /** Blur radius before tracing: smooths noisy renders. */
  readonly smooth: number
}

export const DEFAULT_TRACE: TraceSettings = { colors: 16, smooth: 0 }

/** A colour the picture is unlikely to contain, painted where it is transparent (finishSvg drops its paths: keep both in step). */
const KEY = [255, 0, 255] as const
/** Pixels at least this opaque are kept (as fully opaque). */
const OPAQUE_FROM = 128

/** Paints transparent pixels in the key colour and makes the rest opaque, in place. Returns whether any were transparent. */
export function keyTransparent(data: Uint8ClampedArray): boolean {
  let any = false
  for (let i = 0; i < data.length; i += 4) {
    if (data[i + 3] < OPAQUE_FROM) {
      data[i] = KEY[0]
      data[i + 1] = KEY[1]
      data[i + 2] = KEY[2]
      any = true
    }
    data[i + 3] = 255
  }
  return any
}

/** Drops the key colour's paths and gives the document an explicit pixel size. */
export function finishSvg(svg: string, width: number, height: number): string {
  return svg
    .replace(/<path fill="rgb\(255,0,255\)"[^>]*\/>/g, '')
    .replace(/<svg viewBox=/, `<svg width="${width}" height="${height}" viewBox=`)
}

/** The picture as an SVG document (text) with the same pixel size, so the grid width stays valid. */
export async function traceToSvg(source: Blob, settings: TraceSettings): Promise<string> {
  const { default: tracer } = await import('imagetracerjs')
  const bitmap = await createImageBitmap(source)
  const canvas = document.createElement('canvas')
  canvas.width = bitmap.width
  canvas.height = bitmap.height
  const context = canvas.getContext('2d', { willReadFrequently: true })
  if (!context) throw new Error('The picture could not be traced.')
  context.drawImage(bitmap, 0, 0)
  const pixels = context.getImageData(0, 0, canvas.width, canvas.height)
  keyTransparent(pixels.data)
  const svg = tracer.imagedataToSVG(pixels, {
    numberofcolors: settings.colors,
    blurradius: settings.smooth,
    blurdelta: 32,
    colorquantcycles: 3,
    pathomit: 16,
    roundcoords: 1,
    ltres: 1,
    qtres: 1,
    viewbox: true,
  })
  return finishSvg(svg, canvas.width, canvas.height)
}
