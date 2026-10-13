import { PROJECT_FORMAT, PROJECT_FORMAT_VERSION } from './document'
import type { FolderNode, ObjectNode, ObjectValue, ProjectDocument, ReferenceNode, ValueNode } from './document'

/** The project as a JSON file: parsed strictly (every problem names its path), written with a fixed key order. */

export class ProjectFormatError extends Error {
  /** JSON path of the offending value, e.g. `objects[2].children[0].length`. */
  readonly path: string

  constructor(path: string, message: string) {
    super(`${path}: ${message}`)
    this.name = 'ProjectFormatError'
    this.path = path
  }
}

type Json = unknown

const record = (value: Json, path: string): Record<string, Json> => {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) throw new ProjectFormatError(path, 'expected an object')
  return value as Record<string, Json>
}
const array = (value: Json, path: string): readonly Json[] => {
  if (!Array.isArray(value)) throw new ProjectFormatError(path, 'expected an array')
  return value
}
const string = (value: Json, path: string): string => {
  if (typeof value !== 'string') throw new ProjectFormatError(path, 'expected a string')
  return value
}
const optionalString = (value: Json, path: string): string | undefined => (value === undefined ? undefined : string(value, path))
const boolean = (value: Json, path: string): boolean => {
  if (typeof value !== 'boolean') throw new ProjectFormatError(path, 'expected true or false')
  return value
}
const optionalFlag = (value: Json, path: string): { subscribed?: boolean } => (value === undefined ? {} : { subscribed: boolean(value, path) })
const count = (value: Json, path: string): number => {
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) throw new ProjectFormatError(path, 'expected a whole number >= 0')
  return value
}

const objectValue = (value: Json, path: string): ObjectValue | undefined => {
  if (value === undefined || typeof value === 'string') return value
  const entries = array(value, path)
  if (entries.every((entry) => typeof entry === 'boolean')) return entries as boolean[]
  if (entries.every((entry) => typeof entry === 'number')) return entries as number[]
  throw new ProjectFormatError(path, 'expected a string, a list of numbers, or a list of true / false')
}

/** One tree node; `ids` collects IDs across the whole file so duplicates are caught. */
const parseNode = (value: Json, path: string, ids: Set<string>): ObjectNode => {
  const node = record(value, path)
  const id = string(node.id, `${path}.id`)
  if (!id) throw new ProjectFormatError(`${path}.id`, 'is empty')
  if (ids.has(id)) throw new ProjectFormatError(`${path}.id`, `'${id}' is used twice`)
  ids.add(id)
  const name = string(node.name, `${path}.name`)
  const description = optionalString(node.description, `${path}.description`)
  const described = description === undefined ? {} : { description }
  const kind = string(node.kind, `${path}.kind`)
  if (kind === 'folder') {
    const children = array(node.children, `${path}.children`).map((child, index) => parseNode(child, `${path}.children[${index}]`, ids))
    return { kind, id, name, children, ...optionalFlag(node.subscribed, `${path}.subscribed`), ...described } satisfies FolderNode
  }
  if (kind === 'reference') return { kind, id, name, targetId: string(node.targetId, `${path}.targetId`), ...described } satisfies ReferenceNode
  if (kind === 'value') {
    if (node.typeMode !== undefined && node.typeMode !== 'auto') throw new ProjectFormatError(`${path}.typeMode`, "expected 'auto'")
    const initial = objectValue(node.value, `${path}.value`)
    return {
      kind,
      id,
      name,
      type: string(node.type, `${path}.type`),
      length: count(node.length, `${path}.length`),
      mutable: boolean(node.mutable, `${path}.mutable`),
      retentive: boolean(node.retentive, `${path}.retentive`),
      ...(node.typeMode === 'auto' ? { typeMode: 'auto' as const } : {}),
      ...optionalFlag(node.subscribed, `${path}.subscribed`),
      ...described,
      ...(initial === undefined ? {} : { value: initial }),
    } satisfies ValueNode
  }
  throw new ProjectFormatError(`${path}.kind`, `unknown kind '${kind}' (folder, value or reference)`)
}

export const parseProject = (text: string): ProjectDocument => {
  let root: Json
  try {
    root = JSON.parse(text)
  } catch (error) {
    throw new ProjectFormatError('$', `not JSON (${error instanceof Error ? error.message : String(error)})`)
  }
  const doc = record(root, '$')
  if (doc.format !== PROJECT_FORMAT) throw new ProjectFormatError('format', `expected '${PROJECT_FORMAT}'`)
  const version = count(doc.format_version, 'format_version')
  if (version !== PROJECT_FORMAT_VERSION) throw new ProjectFormatError('format_version', `${version} is not supported (this app reads ${PROJECT_FORMAT_VERSION})`)
  const ids = new Set<string>()
  return {
    format: PROJECT_FORMAT,
    format_version: version,
    name: string(doc.name, 'name'),
    objects: array(doc.objects, 'objects').map((node, index) => parseNode(node, `objects[${index}]`, ids)),
  }
}

const nodeJson = (node: ObjectNode): unknown => {
  const { description, ...rest } = node
  const base = node.kind === 'folder' ? { ...rest, children: node.children.map(nodeJson) } : rest
  return description === undefined ? base : { ...base, description }
}

export const serializeProject = (project: ProjectDocument): string =>
  `${JSON.stringify(
    { format: PROJECT_FORMAT, format_version: PROJECT_FORMAT_VERSION, name: project.name, objects: project.objects.map(nodeJson) },
    null,
    2,
  )}\n`
