import path from 'node:path'
import { describe, expect, it } from 'vitest'
import { resolveWritable } from '../../vite/componentFiles'

const root = path.resolve('/repo/components/devices')

describe('component file endpoint paths', () => {
  it('allows layout files and pictures inside a device folder', () => {
    expect(resolveWritable(root, 'device_pca9685', 'app/canvas/breakout/layout.json')).toBe(
      path.join(root, 'device_pca9685', 'app', 'canvas', 'breakout', 'layout.json'),
    )
    expect(resolveWritable(root, 'device_pca9685', 'app/canvas/breakout/breakout.png')).toBe(
      path.join(root, 'device_pca9685', 'app', 'canvas', 'breakout', 'breakout.png'),
    )
  })

  it('refuses the csv / json that describe the device', () => {
    expect(resolveWritable(root, 'device_pca9685', 'device_pca9685.pins.csv')).toBeNull()
    expect(resolveWritable(root, 'device_pca9685', 'device_pca9685.json')).toBeNull()
    expect(resolveWritable(root, 'device_pca9685', 'device_pca9685.c')).toBeNull()
  })

  it('refuses paths that leave the device folder', () => {
    expect(resolveWritable(root, '..', 'app/canvas/x/layout.json')).toBeNull()
    expect(resolveWritable(root, 'device_pca9685', '../app/canvas/x/layout.json')).toBeNull()
    expect(resolveWritable(root, 'device_pca9685', 'app/canvas/x/../../x.png')).toBeNull()
    expect(resolveWritable(root, 'device_pca9685', 'sub/x/layout.json')).toBeNull()
  })
})
