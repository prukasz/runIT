import { describe, expect, it } from 'vitest'
import { sampleProject } from '../../../src/sampleProject'
import { objectUses } from '../../../src/canvas/blocks/objectUses'

describe('where a variable is used', () => {
  const canvases = sampleProject().canvases!

  it('lists the blocks that read or drive it, with what they do with it', () => {
    const uses = objectUses(canvases, new Set(['count']))
    expect(uses.map((use) => [use.blockId, use.role])).toEqual([
      ['counter', 'Reads Input 0'],
      ['counter', 'Writes Result'],
      ['above', 'Reads Input 0'],
    ])
  })

  it('finds a variable used as a dynamic position', () => {
    expect(objectUses(canvases, new Set(['sel'])).map((use) => use.blockId)).toEqual(['pick', 'route'])
  })

  it('says when only a folder holding it is wired', () => {
    expect(objectUses(canvases, new Set(['nothing']), new Set(['table'])).map((use) => use.role)).toEqual(['Reads Input 0 (in folder)'])
  })
})
