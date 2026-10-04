import { describe, expect, it } from 'vitest'
import { whiteToTransparent } from '../../../src/domain/devices/knockout'

const px = (...rgba: number[]) => new Uint8ClampedArray(rgba)

describe('whiteToTransparent (all white)', () => {
  it('clears pure white and keeps colours at tolerance 0', () => {
    const data = px(255, 255, 255, 255, 20, 100, 40, 255, 200, 200, 200, 255)
    whiteToTransparent(data, 3, 1, 0, false)
    expect([data[3], data[7], data[11]]).toEqual([0, 255, 255])
  })

  it('a higher tolerance also clears near-white, and a pixel fades in between', () => {
    const data = px(240, 240, 240, 255, 247, 247, 247, 255)
    whiteToTransparent(data, 2, 1, 20, false)
    expect([data[3], data[7]]).toEqual([0, 0])
    const edge = px(240, 240, 240, 255)
    whiteToTransparent(edge, 1, 1, 10, false)
    expect(edge[3]).toBeGreaterThan(0)
    expect(edge[3]).toBeLessThan(255)
  })

  it('does not raise the alpha of an already transparent pixel', () => {
    const data = px(0, 0, 0, 0)
    whiteToTransparent(data, 1, 1, 0, false)
    expect(data[3]).toBe(0)
  })
})

describe('whiteToTransparent (outside only)', () => {
  // 5x5: white background, a blue part in the middle with a white dot inside it, and a white pixel enclosed by blue.
  const W = [255, 255, 255, 255]
  const B = [30, 60, 200, 255]
  const grid = [
    W, W, W, W, W,
    W, B, B, B, W,
    W, B, W, B, W,
    W, B, B, B, W,
    W, W, W, W, W,
  ]
  const alphaAt = (data: Uint8ClampedArray, x: number, y: number) => data[(y * 5 + x) * 4 + 3]

  it('removes the background but keeps white inside the part', () => {
    const data = new Uint8ClampedArray(grid.flat())
    whiteToTransparent(data, 5, 5, 0)
    expect(alphaAt(data, 0, 0)).toBe(0)
    expect(alphaAt(data, 4, 2)).toBe(0)
    expect(alphaAt(data, 1, 1)).toBe(255)
    expect(alphaAt(data, 2, 2)).toBe(255)
  })

  it('removes the enclosed white too when asked for all white', () => {
    const data = new Uint8ClampedArray(grid.flat())
    whiteToTransparent(data, 5, 5, 0, false)
    expect(alphaAt(data, 2, 2)).toBe(0)
  })

  it('a high tolerance still keeps light colours inside the part', () => {
    const data = new Uint8ClampedArray(grid.flat())
    data.set([245, 245, 245, 255], (2 * 5 + 2) * 4)
    whiteToTransparent(data, 5, 5, 40)
    expect(alphaAt(data, 2, 2)).toBe(255)
    expect(alphaAt(data, 0, 0)).toBe(0)
  })
})
