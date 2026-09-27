import { packStruct } from '../../backend/packetPack'
import { encodeVmObjectHead } from '../descriptors'
import type { VmCatalog, VmObjectType } from '../descriptors'
import { userSection, walkObjects } from '../project'
import type { FolderNode, ObjectNode, ObjectSection, ProjectDocument, ReferenceNode, ValueNode } from '../project'

/*
 * Objects of a project → VM object IDs, 0x42 records (create) and 0x43 records
 * (initial values, folder links), with the firmware's load rules checked first.
 *
 * Wire IDs: the device numbers objects 0..obj_cnt-1 in their own ID space
 * (accessors and blocks have separate ones). The object space is a list of
 * sections: the user's tree first, then block-owned objects (outputs, ENO).
 * IDs run through the sections
 * in order, each in tree order, a folder before its children. They are not
 * stored anywhere; the returned layout translates both ways (telemetry,
 * error payloads). References may cross sections.
 */

export interface Diagnostic {
  readonly severity: 'error' | 'warning'
  /** Project ID of the object concerned. */
  readonly objectId?: string
  /** Key of the accessor request (a block pin …) concerned. */
  readonly pathKey?: string
  /** Project ID of the block concerned. */
  readonly blockId?: string
  readonly message: string
  /** The firmware error this rule mirrors, if the device would raise one. */
  readonly firmwareError?: string
}

export interface PlacedObject {
  readonly wireId: number
  /** Key of the section the object comes from (`user`, `blocks` …). */
  readonly section: string
  readonly node: ObjectNode
  readonly type: VmObjectType
  readonly elements: number
  /** Bytes in the arena: memory width x elements. */
  readonly payloadSize: number
  readonly name: Uint8Array
  /** Wire IDs of a folder's children, in order. */
  readonly children?: readonly number[]
}

export interface ObjectLayout {
  /** Index = wire ID. */
  readonly objects: readonly PlacedObject[]
  /** Project ID (object or reference) → wire ID. */
  readonly wireIdOf: ReadonlyMap<string, number>
  /** First wire ID and count of each section, in order. */
  readonly sections: readonly { readonly key: string; readonly first: number; readonly count: number }[]
}

const ascii = new TextEncoder()
const PRINTABLE = /^[\x20-\x7e]*$/

const nameProblem = (node: ObjectNode, catalog: VmCatalog, isTopLevel = false): Diagnostic | undefined => {
  if (!node.name) {
    if (isTopLevel) return { severity: 'error', objectId: node.id, message: 'Every top-level object needs a name.' }
    return undefined
  }
  if (!PRINTABLE.test(node.name)) return { severity: 'error', objectId: node.id, message: `'${node.name}': names are plain ASCII (letters, digits, punctuation, space).` }
  if (node.name.length > catalog.nameMax) return { severity: 'error', objectId: node.id, message: `'${node.name}' is ${node.name.length} characters, at most ${catalog.nameMax}.`, firmwareError: 'ERR_VM_OBJ_NAME_TOO_LONG' }
  return undefined
}

/** Folders with nothing to upload under them are left out: a PTR object needs at least one element. */
const hasContent = (node: ObjectNode): boolean => node.kind !== 'folder' || node.children.some(hasContent)

const INTEGER_RANGES: Readonly<Record<string, readonly [number, number]>> = {
  uint8_t: [0, 0xff],
  uint16_t: [0, 0xffff],
  uint32_t: [0, 0xffffffff],
  int32_t: [-0x80000000, 0x7fffffff],
}
const FLOAT32_MAX = 3.4028234663852886e38

/**
 * A value in wire form (0x43 data), or a problem with it. Missing values, and
 * all-zero ones unless `keepZeros` (payloads start zeroed at load), give no bytes.
 */
export const encodeObjectValue = (node: ValueNode, type: VmObjectType, keepZeros = false): { bytes?: Uint8Array; problem?: string } => {
  const { value } = node
  if (value === undefined) return {}
  if (type.key === 'STR') {
    if (typeof value !== 'string') return { problem: 'a text object takes a string value' }
    const bytes = ascii.encode(value)
    if (bytes.byteLength > node.length) return { problem: `the text is ${bytes.byteLength} bytes, the object holds ${node.length}` }
    return bytes.byteLength || keepZeros ? { bytes } : {}
  }
  if (typeof value === 'string') return { problem: `a ${type.alias} takes a list of values, not text` }
  if (value.length > node.length) return { problem: `${value.length} values for ${node.length} element(s)` }
  const isBool = type.key === 'B'
  const numbers: number[] = []
  for (const [index, entry] of value.entries()) {
    if (isBool !== (typeof entry === 'boolean')) return { problem: `value[${index}]: a ${type.alias} takes ${isBool ? 'true / false' : 'numbers'}` }
    const number = typeof entry === 'boolean' ? Number(entry) : entry
    if (type.wireType === 'float') {
      if (!Number.isFinite(number) || Math.abs(number) > FLOAT32_MAX) return { problem: `value[${index}] = ${number} is outside the float range` }
    } else {
      const [min, max] = INTEGER_RANGES[type.wireType] ?? [0, 0]
      if (!Number.isInteger(number) || number < min || number > max) return { problem: `value[${index}] = ${number}: a ${type.alias} is a whole number ${min}..${max}` }
    }
    numbers.push(number)
  }
  if (!numbers.length || (!keepZeros && numbers.every((number) => number === 0))) return {}
  return { bytes: packStruct([{ kind: 'scalar', name: 'value', type: type.wireType, length: numbers.length }], { value: numbers }) }
}

export interface CompiledObjects {
  readonly layout: ObjectLayout
  readonly diagnostics: readonly Diagnostic[]
  /** 0x42 records: u16 id, vm_obj_head_t, name. */
  readonly createRecords: readonly Uint8Array[]
  /** Initial data per object (not yet split), wire form. */
  readonly data: readonly { readonly wireId: number; readonly bytes: Uint8Array; readonly wireWidth: number }[]
  /** Arena bytes of the objects and their registry. */
  readonly arenaBytes: number
  /** Size of the device's retain record, 0 without retentive objects. */
  readonly retainBytes: number
}

const align = (size: number, to: number): number => Math.ceil(size / to) * to

/**
 * Compile the project's objects, followed by `extraSections` (block outputs,
 * attached object files) in the same wire ID space. `driven`: user objects a
 * block writes as its output (mutable, fresh only in the pass that wrote them).
 */
export const compileObjects = (project: ProjectDocument, catalog: VmCatalog, extraSections: readonly ObjectSection[] = [], driven: ReadonlySet<string> = new Set()): CompiledObjects => {
  const sections = [userSection(project), ...extraSections]
  const diagnostics: Diagnostic[] = []
  const placed: PlacedObject[] = []
  const wireIdOf = new Map<string, number>()
  const data: { wireId: number; bytes: Uint8Array; wireWidth: number }[] = []
  const retentiveNames = new Map<string, string>()

  // One index over every section: references resolve across them, IDs must be unique across them.
  const byId = new Map<string, ObjectNode>()
  const sectionKeys = new Set<string>()
  for (const entry of sections) {
    if (sectionKeys.has(entry.key)) diagnostics.push({ severity: 'error', message: `Two object sections named '${entry.key}'.` })
    sectionKeys.add(entry.key)
    walkObjects(entry.objects, (node) => {
      if (byId.has(node.id)) diagnostics.push({ severity: 'error', objectId: node.id, message: `ID '${node.id}' is used twice (section '${entry.key}').` })
      else byId.set(node.id, node)
    })
  }
  const targetOf = (node: ObjectNode): ObjectNode => node.kind === 'reference' ? byId.get(node.targetId) ?? node : node

  const checkSiblings = (nodes: readonly ObjectNode[], where: string): void => {
    const seen = new Set<string>()
    for (const node of nodes) {
      const name = targetOf(node).name
      if (!name) continue
      if (seen.has(name)) diagnostics.push({ severity: 'error', objectId: node.id, message: `Two objects named '${name}' in ${where}: name lookups would find only the first.` })
      seen.add(name)
    }
  }

  // Pass 1: IDs in tree order, so a folder knows its children's IDs.
  let section = sections[0]!
  const place = (nodes: readonly ObjectNode[], isTopLevel = false): void => {
    for (const node of nodes) {
      if (node.kind === 'reference') continue
      const problem = nameProblem(node, catalog, isTopLevel && section.owner === 'user')
      if (problem) diagnostics.push(problem)
      if (node.kind === 'folder') {
        if (!hasContent(node)) {
          diagnostics.push({ severity: 'warning', objectId: node.id, message: `Folder '${node.name}' is empty and is not uploaded.` })
          continue
        }
        wireIdOf.set(node.id, placed.length)
        placed.push(undefined as unknown as PlacedObject) // filled once the children have IDs
        place(node.children)
        checkSiblings(node.children, `folder '${node.name}'`)
        continue
      }
      wireIdOf.set(node.id, placed.length)
      placed.push(placeValue(node))
    }
  }

  const placeValue = (node: ValueNode): PlacedObject => {
    const type = catalog.type(node.type)
    const wireId = placed.length
    const fallback = catalog.types.find((entry) => entry.key === 'U8') ?? catalog.types[0]
    if (!type || type.key === 'PTR') {
      diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}': unknown type '${node.type}' (one of ${catalog.types.filter((entry) => entry.key !== 'PTR').map((entry) => entry.key).join(', ')}).`, firmwareError: 'ERR_VM_OBJ_BAD_TYPE' })
      return { wireId, section: section.key, node, type: fallback, elements: 0, payloadSize: 0, name: ascii.encode(node.name) }
    }
    if (node.length < 1) diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}' has no elements (length 0).`, firmwareError: 'ERR_VM_OBJ_EMPTY' })
    const payloadSize = type.memoryWidth * node.length
    if (payloadSize > catalog.payloadMax) diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}' takes ${payloadSize} bytes, at most ${catalog.payloadMax} (${catalog.maxElements(type)} elements of ${type.alias}).` })
    const { bytes, problem } = encodeObjectValue(node, type)
    if (problem) diagnostics.push({ severity: 'error', objectId: node.id, message: `'${node.name}': ${problem}.`, firmwareError: 'ERR_VM_LOAD_DATA_RANGE' })
    if (bytes) data.push({ wireId, bytes, wireWidth: type.wireWidth })
    if (node.retentive && !node.name) diagnostics.push({ severity: 'error', objectId: node.id, message: 'A value kept after restart needs a name: the device restores retained values by name.', firmwareError: 'ERR_VM_RETAIN_UNNAMED' })
    else if (node.retentive) {
      const other = retentiveNames.get(node.name)
      if (other !== undefined) diagnostics.push({ severity: 'error', objectId: node.id, message: `Two retentive objects named '${node.name}': the device restores retained values by name.` })
      else retentiveNames.set(node.name, node.id)
    }
    return { wireId, section: section.key, node, type, elements: node.length, payloadSize, name: ascii.encode(node.name) }
  }

  const sectionRanges: { key: string; first: number; count: number }[] = []
  for (const entry of sections) {
    section = entry
    const first = placed.length
    place(entry.objects, true)
    sectionRanges.push({ key: entry.key, first, count: placed.length - first })
  }
  checkSiblings(sections.flatMap((entry) => entry.objects), 'the top level')

  const references: ReferenceNode[] = []
  const folders: FolderNode[] = []
  const topLevelIds = new Set(sections.flatMap((entry) => entry.objects.map((node) => node.id)))
  for (const entry of sections) {
    walkObjects(entry.objects, (node) => {
      if (node.kind === 'reference') references.push(node)
      if (node.kind === 'folder') folders.push(node)
    })
  }
  for (const reference of references) {
    if (topLevelIds.has(reference.id)) {
      diagnostics.push({ severity: 'error', objectId: reference.id, message: `Reference '${reference.name || 'unnamed'}' cannot exist at the top level; references must be inside a folder.` })
    }
    const target = byId.get(reference.targetId)
    if (!target || target.kind === 'reference') {
      diagnostics.push({ severity: 'error', objectId: reference.id, message: `Reference '${reference.name}' needs an existing value or folder target.` })
      continue
    }
    const wireId = wireIdOf.get(target.id)
    if (wireId === undefined) diagnostics.push({ severity: 'error', objectId: reference.id, message: `Reference '${reference.name}' points to a folder that is not uploaded.` })
    else wireIdOf.set(reference.id, wireId)
  }

  // A PTR can link to a folder elsewhere; reject cycles before VM telemetry traverses them.
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

  // Pass 2: folders, now that their children have IDs.
  const fillFolders = (nodes: readonly ObjectNode[]): void => {
    for (const node of nodes) {
      if (node.kind !== 'folder' || !wireIdOf.has(node.id)) continue
      fillFolders(node.children)
      const wireId = wireIdOf.get(node.id)!
      const children = node.children.map((child) => wireIdOf.get(child.id)).filter((id): id is number => id !== undefined)
      placed[wireId] = placeFolder(node, wireId, children, section.key)
    }
  }
  const placeFolder = (node: FolderNode, wireId: number, children: readonly number[], sectionKey: string): PlacedObject => {
    const type = catalog.ptrType
    data.push({ wireId, bytes: packStruct([{ kind: 'scalar', name: 'ids', type: 'uint16_t', length: children.length }], { ids: children }), wireWidth: type.wireWidth })
    return { wireId, section: sectionKey, node, type, elements: children.length, payloadSize: type.memoryWidth * children.length, name: ascii.encode(node.name), children }
  }
  for (const entry of sections) {
    section = entry
    fillFolders(entry.objects)
  }

  const limitIds = Math.min(catalog.dynBit, catalog.idNone)
  if (placed.length > limitIds) diagnostics.push({ severity: 'error', message: `${placed.length} objects, the device numbers at most ${limitIds}.`, firmwareError: 'ERR_VM_REG_OOB' })

  const ownerOf = new Map(sections.map((entry) => [entry.key, entry.owner]))
  const createRecords = placed.map((entry) => {
    const retentive = entry.node.kind === 'value' && entry.node.retentive
    const head = encodeVmObjectHead(catalog, {
      payload_size: entry.payloadSize & catalog.payloadMax,
      'd.obj_t': entry.type.value,
      'd.name_size': Math.min(entry.name.byteLength, catalog.nameMax),
      'f.tagged': entry.name.byteLength ? 1 : 0,
      // Folders are linked by 0x43 records, which the device allows only on a mutable PTR (vm_obj_link_direct).
      'f.mutable': entry.node.kind === 'folder' || (entry.node.kind === 'value' && (entry.node.mutable || driven.has(entry.node.id))) ? 1 : 0,
      'f.retentive': retentive ? 1 : 0,
      // Constants and user variables never clear their update flag; block-owned objects are fresh only in the pass that wrote them (VM_EXEC.MD).
      'f.upd_resetable': ownerOf.get(entry.section) === 'block' || driven.has(entry.node.id) ? 1 : 0,
    })
    const record = new Uint8Array(2 + head.byteLength + Math.min(entry.name.byteLength, catalog.nameMax))
    new DataView(record.buffer).setUint16(0, entry.wireId, true)
    record.set(head, 2)
    record.set(entry.name.subarray(0, catalog.nameMax), 2 + head.byteLength)
    return record
  })

  const { alignment, pointer, objectHead } = catalog.arena
  const registry = placed.length ? align(placed.length * pointer, alignment) : 0
  const arenaBytes = placed.reduce((sum, entry) => sum + align(objectHead + entry.payloadSize + Math.min(entry.name.byteLength, catalog.nameMax), alignment), registry)

  const retained = placed.filter((entry) => entry.node.kind === 'value' && entry.node.retentive)
  const retainBytes = retained.length ? retained.reduce((sum, entry) => sum + 4 + entry.name.byteLength + entry.payloadSize, 2) : 0
  if (retainBytes > catalog.retainMaxBytes) diagnostics.push({ severity: 'error', message: `Retained values take ${retainBytes} bytes, the device keeps at most ${catalog.retainMaxBytes}.`, firmwareError: 'ERR_VM_RETAIN_TOO_BIG' })

  return { layout: { objects: placed, wireIdOf, sections: sectionRanges }, diagnostics, createRecords, data, arenaBytes, retainBytes }
}
