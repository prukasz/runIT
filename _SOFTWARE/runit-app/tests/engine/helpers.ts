import { createEngine, newBlock } from '@engine'
import type { BlockInstance, Project, Source, Variable } from '@engine'

/** One engine over the bundled firmware descriptors, shared by the tests (it holds no project). */
export const engine = createEngine()

export const num = (id: string, name = id, extra: Partial<Extract<Variable, { kind: 'value' }>> = {}): Variable => ({ kind: 'value', id, name, type: 'f32', count: 1, ...extra })
export const flag = (id: string, name = id, extra: Partial<Extract<Variable, { kind: 'value' }>> = {}): Variable => ({ kind: 'value', id, name, type: 'bool', count: 1, ...extra })
export const folder = (id: string, children: Variable[], name = id): Variable => ({ kind: 'folder', id, name, children })

/** A source reading a whole variable by id, or a path into it. */
export const read = (root: string, ...steps: Extract<Source, { kind: 'variable' }>['path']['steps'][number][]): Source => ({ kind: 'variable', path: { root, steps } })
export const out = (block: string, pin = 0): Source => ({ kind: 'output', block, pin })
export const eno = (block: string): Source => ({ kind: 'eno', block })

export const blk = (id: string, type: string, patch: Partial<Omit<BlockInstance, 'id' | 'type'>> = {}): BlockInstance => newBlock(id, type, patch)

export const project = (variables: Variable[], blocks: BlockInstance[]): Project => ({ variables, blocks })

/** Compile and fail loudly with the diagnostics when the project does not build. */
export const build = (p: Project) => {
  const result = engine.compile(p)
  if (!result.ok) throw new Error(`Did not compile:\n${result.diagnostics.map((d) => `  ${d.severity} ${d.code}: ${d.message}`).join('\n')}`)
  return result
}
