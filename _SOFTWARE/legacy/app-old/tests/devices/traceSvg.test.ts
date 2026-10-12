import { describe, expect, it } from 'vitest'
import { finishSvg, keyTransparent } from '../../src/devices/traceSvg'

describe('trace helpers', () => {
  it('keys transparent pixels and makes the rest opaque', () => {
    const data = new Uint8ClampedArray([10, 20, 30, 255, 200, 200, 200, 0, 5, 6, 7, 200])
    expect(keyTransparent(data)).toBe(true)
    expect([...data]).toEqual([10, 20, 30, 255, 255, 0, 255, 255, 5, 6, 7, 255])
    expect(keyTransparent(new Uint8ClampedArray([1, 2, 3, 255]))).toBe(false)
  })

  it('drops the key colour paths and sets the pixel size', () => {
    const svg = '<svg viewBox="0 0 4 2" version="1.1"><path fill="rgb(255,0,255)" stroke="rgb(255,0,255)" d="M0 0"/><path fill="rgb(1,2,3)" stroke="rgb(1,2,3)" d="M1 1"/></svg>'
    const out = finishSvg(svg, 4, 2)
    expect(out).not.toContain('255,0,255')
    expect(out).toContain('rgb(1,2,3)')
    expect(out).toContain('<svg width="4" height="2" viewBox=')
  })
})
