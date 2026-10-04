import { serializeBoardTemplate } from '../../domain/project'
import { parseBoardTemplate } from '../../domain/project'
import type { BoardTemplate } from '../../domain/board'

/*
 * The board library (components/boards/<id>.board.json): the templates made in the board creator, bundled into the app
 * to start new boards from, and written by the creator through the dev server. A project keeps its own copy of the board
 * it uses, so the library can change without changing a project.
 */

const files = import.meta.glob<unknown>('../../../../components/boards/*.board.json', { eager: true, import: 'default' })

export interface LibraryBoard {
  readonly template: BoardTemplate
  /** The file's path name, `<id>.board.json`. */
  readonly file: string
}

/** The bundled library boards, by ID; a file that is not a valid template is skipped. */
export const libraryBoards = (): readonly LibraryBoard[] =>
  Object.entries(files)
    .flatMap(([path, raw]) => {
      try {
        return [{ template: parseBoardTemplate(raw, path), file: path.replace(/^.*\//, '') }]
      } catch {
        return []
      }
    })
    .sort((a, b) => a.template.name.localeCompare(b.template.name))

const ROUTE = '/__runit/board-library'

async function check(response: Response): Promise<Response> {
  if (!response.ok) throw new Error(`Board library: ${response.status} ${(await response.text()).trim() || response.statusText}`)
  return response
}

/** Writes the template to the library. The page reloads in dev when a bundled file changes, so call this last. */
export async function saveLibraryBoard(template: BoardTemplate): Promise<void> {
  await check(await fetch(`${ROUTE}/${template.id}.board.json`, { method: 'PUT', body: serializeBoardTemplate(template) }))
}

export async function deleteLibraryBoard(id: string): Promise<void> {
  await check(await fetch(`${ROUTE}/${id}.board.json`, { method: 'DELETE' }))
}
