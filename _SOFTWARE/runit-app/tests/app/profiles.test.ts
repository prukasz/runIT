import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledServiceCatalog } from '../../src/backend'
import type { BoardKind } from '../../src/backend'
import { profileFromBoard } from '../../src/app/devices/boardEditor'
import { parseProject } from '../../src/app/project/projectFile'

/* runit-app/profiles/: the bundled boards' profiles as files, to open in the board editor. They follow board.json and board_devkit.json. */
const catalog = bundledServiceCatalog()

describe.each([['pcb', 'runit-board-r1'], ['devkit', 'esp32s3-devkit']] as [BoardKind, string][])('the %s profile file', (kind, file) => {
  it('is the bundled board\'s profile', () => {
    const read = parseProject(readFileSync(`profiles/${file}.runit-board.json`, 'utf8'))
    expect(read.ok).toBe(true)
    const expected = profileFromBoard(bundledBoard(kind), catalog)
    if (read.ok) {
      expect(read.project.devices).toEqual(expected.devices)
      expect(read.project.i2cBuses).toEqual(expected.i2cBuses)
      expect(read.project.pinAliases).toEqual(expected.pinAliases)
      expect(read.project.reservedPins).toEqual(expected.reservedPins)
      expect(read.project.power).toEqual(expected.power)
    }
  })
})
