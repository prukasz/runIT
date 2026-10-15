import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { createEngine, DEFAULT_LIMITS } from '@engine'
import { limitsFromGenerated, loadLimits } from '@engine/generated'
import { dataChunk, FRAME_OVERHEAD, frameCapacity, frameFill, LIMIT_SYMBOLS } from '@engine/wire/load/limits'
import { blk, build, engine, flag, num, project, read } from '../helpers'

/*
 * The limits come from the firmware: generated/common/limits.json is the `#@limit` options of the Kconfig with the values the
 * firmware is built with (templates/generate_limits.py). These tests hold the engine's numbers against that file, the file
 * against sdkconfig, and the frame arithmetic against the router's C.
 */

const root = fileURLToPath(new URL('../../../../', import.meta.url))
const text = (path: string) => readFileSync(root + path, 'utf8')
const sdkconfig = new Map([...text('sdkconfig').matchAll(/^CONFIG_(\w+)=(-?\d+)$/gm)].map((m) => [m[1]!, Number(m[2])]))

describe('limits from generated/', () => {
  it('every limit the engine checks is published, and equals the value the firmware is built with', () => {
    const published = loadLimits()
    for (const [key, symbol] of Object.entries(LIMIT_SYMBOLS)) {
      expect(published[key as keyof typeof published], `${key} (${symbol})`).toBe(sdkconfig.get(symbol))
    }
  })

  it('the defaults of an engine built by hand are the published limits', () => {
    const published = loadLimits()
    for (const key of Object.keys(LIMIT_SYMBOLS)) {
      expect(DEFAULT_LIMITS[key as keyof typeof DEFAULT_LIMITS], key).toBe(published[key as keyof typeof published])
    }
  })

  it('createEngine uses the published limits, and lets the app override them', () => {
    expect(engine.limits.maxIn).toBe(sdkconfig.get('VM_BLOCK_MAX_IN'))
    expect(engine.limits.frameMax).toBe(sdkconfig.get('ROUTER_FRAME_MAX'))
    expect(createEngine({ limits: { frameMax: 244 } }).limits.frameMax).toBe(244)
    expect(createEngine({ limits: { frameMax: 244 } }).limits.maxIn).toBe(engine.limits.maxIn)
  })

  it('an option the firmware stopped publishing names itself, instead of a number being guessed', () => {
    expect(() => limitsFromGenerated({ limits: {} })).toThrow(/no limit '[A-Z_]+'/)
  })
})

describe('the frame the board takes', () => {
  it('the router puts [seq][flags] and two keys in front of a VM frame', () => {
    const dispatch = text('components/core/router/router_dispatch.c')
    expect(/#define\s+REQ_HDR\s+(\d+)u/.exec(dispatch)?.[1]).toBe('2')
    expect(FRAME_OVERHEAD).toBe(2 + 2)
    expect(frameCapacity(DEFAULT_LIMITS)).toBe(sdkconfig.get('ROUTER_FRAME_MAX')! - 4)
  })

  it('the packing policy stays inside what a frame holds, and gives way to a smaller frame', () => {
    expect(frameFill(DEFAULT_LIMITS)).toBe(DEFAULT_LIMITS.frameBudget)
    expect(frameFill({ ...DEFAULT_LIMITS, frameMax: 128 })).toBe(128 - FRAME_OVERHEAD - 1)
    expect(dataChunk(DEFAULT_LIMITS, 4)).toBe(192)
    expect(dataChunk({ ...DEFAULT_LIMITS, frameMax: 64 }, 4)).toBe(52) // 64 - 4 overhead - 1 count - 6 record head = 53, whole 4-byte elements
    expect(dataChunk({ ...DEFAULT_LIMITS, frameMax: 12 }, 4)).toBe(4) // never less than one element
  })

  it('a program loaded for a small frame has no frame past it', () => {
    const small = createEngine({ limits: { frameMax: 64 } })
    const many = Array.from({ length: 12 }, (_, i) => num(`v${i}`))
    const result = small.compile(project(many, [blk('s', 'set', { inputs: [read('v0'), read('v1')] })]))
    expect(result.ok, JSON.stringify(result.diagnostics)).toBe(true)
    for (const frame of result.frames) expect(frame.payload.length).toBeLessThanOrEqual(frameCapacity(small.limits))
    // the same program for the full frame needs fewer frames
    const full = build(project(many, [blk('s', 'set', { inputs: [read('v0'), read('v1')] })]))
    expect(result.frames.length).toBeGreaterThan(full.frames.length)
  })

  it('a block record that cannot fit a frame is an error, not a frame the board would drop', () => {
    const small = createEngine({ limits: { frameMax: 64 } })
    const result = small.compile(project([flag('go')], [blk('io1', 'io', { variant: 'toggle', device: 1, values: { pin: 1 } })]))
    expect(result.ok).toBe(false)
    expect(result.diagnostics.map((d) => d.code)).toContain('ERR_ROUTER_FRAME_LEN')
    expect(result.frames).toEqual([])
  })
})
