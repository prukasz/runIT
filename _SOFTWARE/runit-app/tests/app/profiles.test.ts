import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog } from '../../src/backend'
import { profileFromBoard, withBoard } from '../../src/app/devices/boardEditor'
import { parseProject, serializeProject } from '../../src/app/project/projectFile'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'
import type { BoardKind } from '../../src/app/devices/boardDescriptor'

/* runit-app/profiles/: the bundled boards' profiles as files, to open in the board editor. They follow board.json and board_devkit.json. */
const catalog = bundledServiceCatalog()

describe.each([['pcb', 'runit-board-r1'], ['devkit', 'esp32s3-devkit']] as [BoardKind, string][])('the %s profile file', (kind, file) => {
  // The board keeps these bytes and the app tells its profile by their CRC (boardHoldsJson).
  it('read back and written again is the same bytes', () => {
    const text = serializeProject(profileFromBoard(bundledBoard(kind), catalog))
    const read = parseProject(text)
    expect(read.ok && serializeProject(withBoard(read.project, bundledBoard()))).toBe(text)
  })

  it('is the bundled board\'s profile', () => {
    const read = parseProject(readFileSync(`profiles/${file}.runit-board.json`, 'utf8'))
    expect(read.ok).toBe(true)
    const expected = profileFromBoard(bundledBoard(kind), catalog)
    if (read.ok) {
      expect(read.project.devices).toEqual(expected.devices)
      expect(read.project.pinAliases).toEqual(expected.pinAliases)
      expect(read.project.reservedPins).toEqual(expected.reservedPins)
      expect(read.project.power).toEqual(expected.power)
    }
  })
})
