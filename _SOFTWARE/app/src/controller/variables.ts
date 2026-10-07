import { walkObjects } from '../domain/project'
import type { ProjectDocument } from '../domain/project'
import type { LiveValue } from '../domain/compiler'

/** A variable a widget can be linked to: one value, not a folder or a list. */
export interface ControllerVariable {
  readonly id: string
  /** The name as the user reads it, with the folders it sits in (`motors.left`). */
  readonly name: string
  readonly type: string
  /** The program may write it (a constant cannot be set by a widget). */
  readonly mutable: boolean
}

export const controllerVariables = (project: ProjectDocument): readonly ControllerVariable[] => {
  const found: ControllerVariable[] = []
  walkObjects(project.objects, (node, ancestors) => {
    if (node.kind !== 'value' || node.length !== 1 || node.type === 'STR') return
    found.push({ id: node.id, name: [...ancestors.map((folder) => folder.name), node.name].join('.'), type: node.type, mutable: node.mutable })
  })
  return found
}

/** What a widget makes of a live value: one number, a flag as 0 / 1, the first of a list; nothing for text or no value. */
export const numberOfLive = (value: LiveValue | undefined): number | undefined => {
  const first = Array.isArray(value) ? value[0] : value
  if (typeof first === 'boolean') return first ? 1 : 0
  return typeof first === 'number' && Number.isFinite(first) ? first : undefined
}
