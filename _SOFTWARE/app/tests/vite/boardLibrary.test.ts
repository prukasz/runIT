import path from 'node:path'
import { describe, expect, it } from 'vitest'
import { resolveBoardFile } from '../../vite/boardLibrary'

const root = path.resolve('/repo/components/boards')

describe('board library endpoint paths', () => {
  it('allows <id>.board.json directly in the folder', () => {
    expect(resolveBoardFile(root, 'lab.board.json')).toBe(path.join(root, 'lab.board.json'))
    expect(resolveBoardFile(root, 'my_board_2.board.json')).toBe(path.join(root, 'my_board_2.board.json'))
  })

  it('refuses other files, folders and paths that leave the folder', () => {
    for (const name of ['lab.json', 'Lab.board.json', '1lab.board.json', '../lab.board.json', 'sub/lab.board.json', '..%2Flab.board.json', 'lab.board.json.exe', '.board.json', '']) {
      expect(resolveBoardFile(root, name)).toBeNull()
    }
  })
})
