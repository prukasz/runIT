/** Width (0..255 levels) of the soft edge between kept and transparent, so antialiased outlines do not turn jagged. */
const FEATHER = 32

/**
 * Makes white transparent in RGBA pixel data, in place. A pixel whose darkest channel is at or above `255 - tolerance`
 * becomes fully transparent; just below that it fades in over a short band, so tolerance 0 removes pure white only.
 *
 * With `outsideOnly` only the white connected to the picture's edge goes (the background): white inside the part, such
 * as silkscreen text or a white cap, stays. A tolerance can then be high without eating the part itself.
 */
export function whiteToTransparent(data: Uint8ClampedArray, width: number, height: number, tolerance: number, outsideOnly = true): void {
  const top = 255 - Math.min(255, Math.max(0, tolerance))
  const lightest = (pixel: number): number => Math.min(data[pixel * 4], data[pixel * 4 + 1], data[pixel * 4 + 2])
  const fade = (pixel: number): void => {
    const level = lightest(pixel)
    const keep = level >= top ? 0 : (top - level) / FEATHER
    data[pixel * 4 + 3] = Math.round(data[pixel * 4 + 3] * keep)
  }
  const candidate = (pixel: number): boolean => lightest(pixel) > top - FEATHER

  if (!outsideOnly) {
    for (let pixel = 0; pixel < width * height; pixel++) if (candidate(pixel)) fade(pixel)
    return
  }

  const reached = new Uint8Array(width * height)
  const stack: number[] = []
  const visit = (pixel: number): void => {
    if (reached[pixel] || !candidate(pixel)) return
    reached[pixel] = 1
    stack.push(pixel)
  }
  for (let x = 0; x < width; x++) {
    visit(x)
    visit((height - 1) * width + x)
  }
  for (let y = 0; y < height; y++) {
    visit(y * width)
    visit(y * width + width - 1)
  }
  while (stack.length) {
    const pixel = stack.pop() as number
    const x = pixel % width
    fade(pixel)
    if (x > 0) visit(pixel - 1)
    if (x < width - 1) visit(pixel + 1)
    if (pixel >= width) visit(pixel - width)
    if (pixel < width * (height - 1)) visit(pixel + width)
  }
}
