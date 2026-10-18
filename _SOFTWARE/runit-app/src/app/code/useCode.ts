import { useMemo, useState } from 'react'
import { addVariable, checkVariables, createEngine, flattenVariables, locate, moveVariable, removeVariable, updateVariable } from '@engine'
import type { ValueType, Variable } from '@engine'
import type { ProjectFile } from '../project/projectFile'
import type { useProject } from '../project/useProject'
import { newVariable, newVariableId, STR_LENGTH } from './variables'
import type { NewKind } from './variables'

type Patch = Parameters<typeof updateVariable>[2]

/**
 * The Code view's variables: the project's tree, the selected variable, the closed folders, and the edits (each one an undo
 * step of the project; typing in one field is one step, by its key).
 */
export function useCode(project: ReturnType<typeof useProject>) {
  const program = project.project.program
  const tree = program.variables
  const engine = useMemo(() => createEngine(), [])
  /** A block as the user calls it: its name, else its type's. */
  const blockLabel = (id: string): string => {
    const block = program.blocks.find((b) => b.id === id)
    return block?.name || (block && engine.catalog.get(block.type)?.alias) || block?.type || id
  }
  const [selectedId, setSelectedId] = useState<string>()
  const [closed, setClosed] = useState<ReadonlySet<string>>(new Set())
  const problems = useMemo(() => checkVariables(tree), [tree])
  const selected = selectedId === undefined ? undefined : locate(tree, selectedId)

  const setTree = (change: (tree: readonly Variable[]) => Variable[], key?: string) =>
    project.edit((p: ProjectFile) => ({ ...p, program: { ...p.program, variables: change(p.program.variables) } }), key)

  /** Selects a variable and opens the folders it is in. */
  const select = (id: string | undefined) => {
    setSelectedId(id)
    const ancestors = id === undefined ? [] : locate(tree, id)?.ancestors ?? []
    if (ancestors.some((f) => closed.has(f.id))) setClosed((c) => new Set([...c].filter((f) => !ancestors.some((a) => a.id === f))))
  }

  /** A new variable in the selected folder, or beside the selected variable; selected at once. */
  const add = (kind: NewKind) => {
    const parent = selected?.variable.kind === 'folder' ? selected.variable : selected?.parent
    const variable = newVariable(kind, tree, parent ? parent.children : tree)
    setTree((t) => addVariable(t, parent?.id ?? null, variable))
    if (parent) setClosed((c) => new Set([...c].filter((f) => f !== parent.id)))
    setSelectedId(variable.id)
  }

  const remove = (id: string) => {
    const gone = locate(tree, id)
    setTree((t) => removeVariable(t, id))
    if (gone && selectedId !== undefined && flattenVariables([gone.variable]).some((v) => v.id === selectedId)) setSelectedId(undefined)
  }

  /** A new top-level variable with this name and type (a pin asked for it); returns its id. */
  const create = (name: string, type: ValueType): string => {
    const variable: Variable = { kind: 'value', id: newVariableId(tree), name, type, count: type === 'str' ? STR_LENGTH : 1 }
    setTree((t) => addVariable(t, null, variable))
    return variable.id
  }

  const update = (id: string, patch: Patch, key?: string) => setTree((t) => updateVariable(t, id, patch), key)

  /** Into a folder (null: the top) at `index`; a folder never into itself. */
  const move = (id: string, parent: string | null, index?: number) => {
    const location = locate(tree, id)
    if (!location || (parent !== null && flattenVariables([location.variable]).some((v) => v.id === parent))) return
    setTree((t) => moveVariable(t, id, parent, index))
    if (parent !== null) setClosed((c) => new Set([...c].filter((f) => f !== parent)))
  }

  const toggle = (id: string) => setClosed((c) => {
    const next = new Set(c)
    if (!next.delete(id)) next.add(id)
    return next
  })

  return { engine, program, tree, blockLabel, selected, select, problems, closed, toggle, add, create, remove, update, move }
}

export type CodeState = ReturnType<typeof useCode>
