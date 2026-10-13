import { NAME_MAX, PAYLOAD_MAX, maxElementsOf, typeOf, valueTypes } from './catalog'
import { walkObjects } from '../project'
import type { FolderNode, ObjectNode, ObjectValue, ProjectDocument, ReferenceNode, ValueNode } from '../project'

/** What the board would refuse in the objects, found before anything is uploaded. */
export interface Diagnostic {
  readonly severity: 'error' | 'warning'
  /** Project ID of the object concerned. */
  readonly objectId?: string
  readonly message: string
}

const PRINTABLE = /^[\x20-\x7e]*$/
const INTEGER_RANGES: Readonly<Record<string, readonly [number, number]>> = {
  uint8_t: [0, 0xff],
  uint16_t: [0, 0xffff],
  uint32_t: [0, 0xffffffff],
  int32_t: [-0x80000000, 0x7fffffff],
}
const FLOAT32_MAX = 3.4028234663852886e38

/** Folders with nothing to upload under them are left out: a PTR object needs at least one element. */
const hasContent = (node: ObjectNode): boolean => node.kind !== 'folder' || node.children.some(hasContent)

const nameProblem = (node: ObjectNode, isTopLevel: boolean): Diagnostic | undefined => {
  if (!node.name) return isTopLevel ? { severity: 'error', objectId: node.id, message: 'Every top-level object needs a name.' } : undefined
  if (!PRINTABLE.test(node.name)) return { severity: 'error', objectId: node.id, message: `'${node.name}': names are plain ASCII (letters, digits, punctuation, space).` }
  if (node.name.length > NAME_MAX) return { severity: 'error', objectId: node.id, message: `'${node.name}' is ${node.name.length} characters, at most ${NAME_MAX}.` }
  return undefined
}

/** The problem with an initial value, if the board would refuse it. */
export const valueProblem = (node: ValueNode): string | undefined => {
  const { value } = node
  const type = typeOf(node.type)
  if (value === undefined || !type) return undefined
  if (type.key === 'STR') {
    if (typeof value !== 'string') return 'a text object takes a string value'
    const bytes = new TextEncoder().encode(value).byteLength
    return bytes > node.length ? `the text is ${bytes} bytes, the object holds ${node.length}` : undefined
  }
  if (typeof value === 'string') return `a ${type.alias} takes a list of values, not text`
  const list: ObjectValue = value
  if (list.length > node.length) return `${list.length} values for ${node.length} element(s)`
  const isBool = type.key === 'B'
  for (const [index, entry] of list.entries()) {
    if (isBool !== (typeof entry === 'boolean')) return `value[${index}]: a ${type.alias} takes ${isBool ? 'true / false' : 'numbers'}`
    const number = typeof entry === 'boolean' ? Number(entry) : entry
    if (type.wireType === 'float') {
      if (!Number.isFinite(number) || Math.abs(number) > FLOAT32_MAX) return `value[${index}] = ${number} is outside the float range`
    } else {
      const [min, max] = INTEGER_RANGES[type.wireType] ?? [0, 0]
      if (!Number.isInteger(number) || number < min || number > max) return `value[${index}] = ${number}: a ${type.alias} is a whole number ${min}..${max}`
    }
  }
  return undefined
}

export const checkObjects = (project: ProjectDocument): readonly Diagnostic[] => {
  const diagnostics: Diagnostic[] = []
  const byId = new Map<string, ObjectNode>()
  walkObjects(project.objects, (node) => {
    if (byId.has(node.id)) diagnostics.push({ severity: 'error', objectId: node.id, message: `ID '${node.id}' is used twice.` })
    else byId.set(node.id, node)
  })
  const targetOf = (node: ObjectNode): ObjectNode => (node.kind === 'reference' ? byId.get(node.targetId) ?? node : node)

  const checkSiblings = (nodes: readonly ObjectNode[], where: string): void => {
    const seen = new Set<string>()
    for (const node of nodes) {
      const name = targetOf(node).name
      if (!name) continue
      if (seen.has(name)) diagnostics.push({ severity: 'error', objectId: node.id, message: `Two objects named '${name}' in ${where}: name lookups would find only the first.` })
      seen.add(name)
    }
  }

  const retentiveNames = new Set<string>()
  const checkValue = (node: ValueNode): void => {
    const type = typeOf(node.type)
    if (!type || type.key === 'PTR') {
      diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}': unknown type '${node.type}' (one of ${valueTypes.map((entry) => entry.key).join(', ')}).` })
      return
    }
    if (node.length < 1) diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}' has no elements (length 0).` })
    const payload = type.memoryWidth * node.length
    if (payload > PAYLOAD_MAX) diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}' takes ${payload} bytes, at most ${PAYLOAD_MAX} (${maxElementsOf(type.key)} elements of ${type.alias}).` })
    const problem = valueProblem(node)
    if (problem) diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}': ${problem}.` })
    if (node.retentive && !node.name) diagnostics.push({ severity: 'error', objectId: node.id, message: 'A value kept after restart needs a name: the device restores retained values by name.' })
    else if (node.retentive) {
      if (retentiveNames.has(node.name)) diagnostics.push({ severity: 'error', objectId: node.id, message: `Two retentive objects named '${node.name}': the device restores retained values by name.` })
      retentiveNames.add(node.name)
    }
  }

  const visit = (nodes: readonly ObjectNode[], isTopLevel: boolean): void => {
    for (const node of nodes) {
      if (node.kind === 'reference') continue
      const problem = nameProblem(node, isTopLevel)
      if (problem) diagnostics.push(problem)
      if (node.kind === 'folder') {
        if (!hasContent(node)) {
          diagnostics.push({ severity: 'warning', objectId: node.id, message: `Folder '${node.name}' is empty and is not uploaded.` })
          continue
        }
        visit(node.children, false)
        checkSiblings(node.children, `folder '${node.name}'`)
      } else checkValue(node)
    }
  }
  visit(project.objects, true)
  checkSiblings(project.objects, 'the top level')

  const references: ReferenceNode[] = []
  const folders: FolderNode[] = []
  walkObjects(project.objects, (node) => {
    if (node.kind === 'reference') references.push(node)
    if (node.kind === 'folder') folders.push(node)
  })
  const topLevel = new Set(project.objects.map((node) => node.id))
  for (const reference of references) {
    if (topLevel.has(reference.id)) diagnostics.push({ severity: 'error', objectId: reference.id, message: `Reference '${reference.name || 'unnamed'}' cannot exist at the top level; references must be inside a folder.` })
    const target = byId.get(reference.targetId)
    if (!target || target.kind === 'reference') diagnostics.push({ severity: 'error', objectId: reference.id, message: `Reference '${reference.name}' needs an existing value or folder target.` })
  }

  // A PTR can link to a folder elsewhere; cycles are refused before telemetry traverses them.
  const visiting = new Set<string>()
  const visited = new Set<string>()
  const visitFolder = (folder: FolderNode): void => {
    if (visited.has(folder.id)) return
    visiting.add(folder.id)
    for (const child of folder.children) {
      const target = targetOf(child)
      if (target.kind !== 'folder') continue
      if (visiting.has(target.id)) diagnostics.push({ severity: 'error', objectId: child.id, message: `Reference to '${target.name}' creates a folder cycle.` })
      else visitFolder(target)
    }
    visiting.delete(folder.id)
    visited.add(folder.id)
  }
  for (const folder of folders) visitFolder(folder)
  return diagnostics
}
