/*
 * A photo of a part into a simple drawing, pure and local (no network, no model): the background is taken away by a flood fill
 * from the points the user clicks (the corners at first) within a colour tolerance; what is left is cut into a few colours
 * (k-means); each colour's areas are traced round (Moore neighbourhood) and simplified (Douglas-Peucker) into SVG paths, the
 * whole silhouette first, in millimetres: the user says how wide the part really is.
 */

export interface Pixels {
  readonly width: number
  readonly height: number
  /** RGBA, row by row. */
  readonly data: Uint8ClampedArray
}

type Pt = { x: number; y: number }

const rgbAt = (img: Pixels, i: number): [number, number, number] => [img.data[i * 4]!, img.data[i * 4 + 1]!, img.data[i * 4 + 2]!]
const dist = (a: readonly number[], b: readonly number[]): number => Math.hypot(a[0]! - b[0]!, a[1]! - b[1]!, a[2]! - b[2]!)

/**
 * The part's mask (1: the part, 0: background): a flood fill from each seed through the pixels within `tolerance` (0-255, colour
 * distance) of that seed's colour; transparent pixels are background too. Then one pixel of closing, so a thin edge stays whole.
 */
export const maskOf = (img: Pixels, seeds: readonly Pt[], tolerance: number): Uint8Array => {
  const { width: w, height: h } = img
  const bg = new Uint8Array(w * h)
  for (let i = 0; i < w * h; i++) if (img.data[i * 4 + 3]! < 16) bg[i] = 1
  for (const seed of seeds) {
    const sx = Math.round(seed.x)
    const sy = Math.round(seed.y)
    if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue
    const ref = rgbAt(img, sy * w + sx)
    const stack = [sy * w + sx]
    while (stack.length > 0) {
      const i = stack.pop()!
      if (bg[i] === 2) continue
      if (dist(rgbAt(img, i), ref) > tolerance && bg[i] !== 1) continue
      bg[i] = 2
      const x = i % w
      if (x > 0) stack.push(i - 1)
      if (x < w - 1) stack.push(i + 1)
      if (i >= w) stack.push(i - w)
      if (i < w * (h - 1)) stack.push(i + w)
    }
  }
  const mask = new Uint8Array(w * h)
  for (let i = 0; i < w * h; i++) mask[i] = bg[i] ? 0 : 1
  return close(mask, w, h)
}

const close = (mask: Uint8Array, w: number, h: number): Uint8Array => {
  const morph = (src: Uint8Array, grow: boolean) => {
    const out = new Uint8Array(src.length)
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      let v = grow ? 0 : 1
      for (const [dx, dy] of [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1]] as const) {
        const nx = x + dx
        const ny = y + dy
        const m = nx < 0 || ny < 0 || nx >= w || ny >= h ? 0 : src[ny * w + nx]!
        v = grow ? Math.max(v, m) : Math.min(v, m)
      }
      out[y * w + x] = v
    }
    return out
  }
  return morph(morph(mask, true), false)
}

/** `k` colours of the part's pixels (k-means), and each pixel's colour (-1: background). */
export const quantize = (img: Pixels, mask: Uint8Array, k: number): { palette: [number, number, number][]; labels: Int16Array } => {
  const idx: number[] = []
  for (let i = 0; i < mask.length; i++) if (mask[i]) idx.push(i)
  const labels = new Int16Array(mask.length).fill(-1)
  if (idx.length === 0) return { palette: [], labels }
  const step = Math.max(1, Math.floor(idx.length / 4000))
  const sample = idx.filter((_, n) => n % step === 0).map((i) => rgbAt(img, i))
  let palette: [number, number, number][] = Array.from({ length: k }, (_, n) => sample[Math.floor(((n + 0.5) / k) * sample.length)]!)
  for (let round = 0; round < 10; round++) {
    const sums = palette.map(() => [0, 0, 0, 0])
    for (const c of sample) {
      let best = 0
      for (let j = 1; j < palette.length; j++) if (dist(c, palette[j]!) < dist(c, palette[best]!)) best = j
      const s = sums[best]!
      s[0]! += c[0]; s[1]! += c[1]; s[2]! += c[2]; s[3]! += 1
    }
    palette = palette.map((p, j) => { const s = sums[j]!; return s[3]! > 0 ? [s[0]! / s[3]!, s[1]! / s[3]!, s[2]! / s[3]!] as [number, number, number] : p })
  }
  for (const i of idx) {
    const c = rgbAt(img, i)
    let best = 0
    for (let j = 1; j < palette.length; j++) if (dist(c, palette[j]!) < dist(c, palette[best]!)) best = j
    labels[i] = best
  }
  return { palette, labels }
}

const DIRS: readonly Pt[] = [{ x: -1, y: 0 }, { x: -1, y: -1 }, { x: 0, y: -1 }, { x: 1, y: -1 }, { x: 1, y: 0 }, { x: 1, y: 1 }, { x: 0, y: 1 }, { x: -1, y: 1 }]

/** The outline of the area holding `start` (its top-left pixel), clockwise. */
const traceOutline = (inside: (x: number, y: number) => boolean, start: Pt): Pt[] => {
  const out: Pt[] = [start]
  let c = start
  let back = { x: start.x - 1, y: start.y }
  for (let guard = 0; guard < 200000; guard++) {
    const bdx = back.x - c.x
    const bdy = back.y - c.y
    const from = DIRS.findIndex((d) => d.x === bdx && d.y === bdy)
    let next: Pt | undefined
    let prev = back
    for (let k = 1; k <= 8; k++) {
      const d = DIRS[(from + k) % 8]!
      const n = { x: c.x + d.x, y: c.y + d.y }
      if (inside(n.x, n.y)) { next = n; break }
      prev = n
    }
    if (!next) break
    back = prev
    c = next
    if (c.x === start.x && c.y === start.y) break
    out.push(c)
  }
  return out
}

/** Fewer points along a closed outline: none further than `epsilon` from the line it is dropped for. */
export const simplify = (points: readonly Pt[], epsilon: number): Pt[] => {
  if (points.length < 4) return [...points]
  const dp = (pts: readonly Pt[]): Pt[] => {
    if (pts.length < 3) return [...pts]
    const [a, b] = [pts[0]!, pts[pts.length - 1]!]
    let worst = 0
    let at = 0
    for (let i = 1; i < pts.length - 1; i++) {
      const p = pts[i]!
      const d = Math.abs((b.y - a.y) * p.x - (b.x - a.x) * p.y + b.x * a.y - b.y * a.x) / (Math.hypot(b.x - a.x, b.y - a.y) || 1)
      if (d > worst) { worst = d; at = i }
    }
    return worst > epsilon ? [...dp(pts.slice(0, at + 1)).slice(0, -1), ...dp(pts.slice(at))] : [a, b]
  }
  // Split the ring at the point furthest from the first, so both halves are open lines.
  let far = 0
  for (let i = 1; i < points.length; i++) if (Math.hypot(points[i]!.x - points[0]!.x, points[i]!.y - points[0]!.y) > Math.hypot(points[far]!.x - points[0]!.x, points[far]!.y - points[0]!.y)) far = i
  return [...dp(points.slice(0, far + 1)).slice(0, -1), ...dp([...points.slice(far), points[0]!]).slice(0, -1)]
}

/** The areas of one label (4-connected) at least `minArea` pixels big, each as its outline. */
const regions = (labels: Int16Array | Uint8Array, w: number, h: number, value: number, minArea: number): { outline: Pt[]; area: number }[] => {
  const seen = new Uint8Array(w * h)
  const out: { outline: Pt[]; area: number }[] = []
  const inside = (x: number, y: number) => x >= 0 && y >= 0 && x < w && y < h && labels[y * w + x] === value
  for (let i = 0; i < w * h; i++) {
    if (seen[i] || labels[i] !== value) continue
    let area = 0
    const stack = [i]
    seen[i] = 1
    while (stack.length > 0) {
      const j = stack.pop()!
      area++
      const x = j % w
      for (const n of [x > 0 ? j - 1 : -1, x < w - 1 ? j + 1 : -1, j - w, j + w]) {
        if (n >= 0 && n < w * h && !seen[n] && labels[n] === value) { seen[n] = 1; stack.push(n) }
      }
    }
    if (area >= minArea) out.push({ outline: traceOutline(inside, { x: i % w, y: Math.floor(i / w) }), area })
  }
  return out
}

const hex = (c: readonly number[]): string => `#${c.map((v) => Math.round(v).toString(16).padStart(2, '0')).join('')}`

export interface Traced {
  readonly svg: string
  readonly w_mm: number
  readonly h_mm: number
}

/**
 * The drawing of a photo: the part's silhouette (cropped to it) in its main colour, then the areas of the other colours over it,
 * each simplified; `width_mm` is how wide the part really is.
 */
export const vectorize = (img: Pixels, mask: Uint8Array, opts: { colours: number; width_mm: number; detail: number }): Traced | undefined => {
  const { width: w, height: h } = img
  let [x0, y0, x1, y1] = [w, h, -1, -1]
  for (let i = 0; i < w * h; i++) if (mask[i]) { const x = i % w; const y = Math.floor(i / w); x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y) }
  if (x1 < 0) return undefined
  const scale = opts.width_mm / (x1 - x0 + 1)
  const w_mm = Math.round(opts.width_mm * 100) / 100
  const h_mm = Math.round((y1 - y0 + 1) * scale * 100) / 100
  const epsilon = Math.max(0.5, 3 - opts.detail)
  const minArea = Math.max(6, Math.round((w * h) / (400 * Math.max(1, opts.detail))))
  const path = (outline: readonly Pt[]) => {
    const pts = simplify(outline, epsilon)
    return `M${pts.map((p) => `${((p.x - x0 + 0.5) * scale).toFixed(2)} ${((p.y - y0 + 0.5) * scale).toFixed(2)}`).join(' L')}Z`
  }
  const { palette, labels } = quantize(img, mask, Math.max(1, Math.min(8, opts.colours)))
  const counts = palette.map((_, j) => labels.reduce((n, l) => n + (l === j ? 1 : 0), 0))
  const main = counts.indexOf(Math.max(...counts))
  const body = regions(mask, w, h, 1, minArea).map((r) => `<path d="${path(r.outline)}" fill="${hex(palette[main] ?? [128, 128, 128])}"/>`)
  const layers = palette.map((c, j) => ({ c, j, rs: j === main ? [] : regions(labels, w, h, j, minArea) }))
    .flatMap(({ c, rs }) => rs.map((r) => ({ c, r }))).sort((a, b) => b.r.area - a.r.area)
    .map(({ c, r }) => `<path d="${path(r.outline)}" fill="${hex(c)}"/>`)
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w_mm} ${h_mm}" width="${w_mm}mm" height="${h_mm}mm">\n${[...body, ...layers].join('\n')}\n</svg>\n`
  return { svg, w_mm, h_mm }
}

/** A picture file read into pixels, at most `max` pixels wide or high. */
export const readImage = async (file: Blob, max = 360): Promise<Pixels> => {
  const bitmap = await createImageBitmap(file)
  const k = Math.min(1, max / Math.max(bitmap.width, bitmap.height))
  const canvas = document.createElement('canvas')
  canvas.width = Math.max(1, Math.round(bitmap.width * k))
  canvas.height = Math.max(1, Math.round(bitmap.height * k))
  const g = canvas.getContext('2d')!
  g.drawImage(bitmap, 0, 0, canvas.width, canvas.height)
  const data = g.getImageData(0, 0, canvas.width, canvas.height)
  return { width: data.width, height: data.height, data: data.data }
}

/** Pixels (and their mask: the background see-through) as a picture to show. */
export const pixelsUrl = (img: Pixels, mask?: Uint8Array): string => {
  const canvas = document.createElement('canvas')
  canvas.width = img.width
  canvas.height = img.height
  const data = new ImageData(new Uint8ClampedArray(img.data), img.width, img.height)
  if (mask) for (let i = 0; i < mask.length; i++) if (!mask[i]) data.data[i * 4 + 3] = 40
  canvas.getContext('2d')!.putImageData(data, 0, 0)
  return canvas.toDataURL()
}
