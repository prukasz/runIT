import { describe, expect, it } from 'vitest'
import { addObject, createProject, findObject, moveObject, parseProject, removeObject, serializeProject, updateObject } from '../../../src/domain/project'
import type { ObjectNode, ProjectDocument } from '../../../src/domain/project'

const tree: ProjectDocument = {
  ...createProject('demo'),
  objects: [
    { kind: 'folder', id: 'f1', name: 'motor', description: 'drive', children: [{ kind: 'value', id: 'v1', name: 'speed', type: 'F', length: 1, mutable: true, retentive: false, value: [0.5] }] },
    { kind: 'value', id: 'v2', name: 'label', type: 'STR', length: 8, mutable: false, retentive: false, value: 'hi' },
    { kind: 'value', id: 'v3', name: 'on', type: 'B', length: 2, mutable: true, retentive: true, value: [true, false] },
  ],
}

describe('project file', () => {
  it('round-trips app-only aliases for board and user devices', () => {
    const project = { ...tree, deviceAliases: { 'board:0': 'Front switches', 'device-1': 'Servo controller' } }
    expect(parseProject(serializeProject(project))).toEqual(project)
    expect(parseProject(JSON.stringify({ ...tree, deviceAliases: { 'board:0': '  Front switches  ', unused: '  ' } })).deviceAliases).toEqual({ 'board:0': 'Front switches' })
    expect(() => parseProject(JSON.stringify({ ...tree, deviceAliases: { 'board:0': 12 } }))).toThrow(/deviceAliases.board:0: expected a string/)
  })

  it('saves and loads back the same document', () => {
    const text = serializeProject(tree)
    expect(parseProject(text)).toEqual(tree)
    expect(serializeProject(parseProject(text))).toBe(text)
    expect(text.endsWith('\n')).toBe(true)
  })

  it('names the first bad value', () => {
    expect(() => parseProject('nope')).toThrow(/^\$: not JSON/)
    expect(() => parseProject('{"format":"other"}')).toThrow(/^format:/)
    expect(() => parseProject(JSON.stringify({ ...tree, format_version: 99 }))).toThrow(/format_version: 99 is not supported/)
    const broken = JSON.parse(serializeProject(tree))
    broken.objects[0].children[0].length = -1
    expect(() => parseProject(JSON.stringify(broken))).toThrow(/^objects\[0\]\.children\[0\]\.length:/)
    broken.objects[0].children[0].length = 1
    broken.objects[1].id = 'v1'
    expect(() => parseProject(JSON.stringify(broken))).toThrow(/objects\[1\]\.id: 'v1' is used twice/)
  })
})

describe('object tree edits', () => {
  const extra: ObjectNode = { kind: 'value', id: 'v4', name: 'dir', type: 'I32', length: 1, mutable: true, retentive: false }

  it('adds, updates, moves and removes without touching the original', () => {
    const added = addObject(tree, 'f1', extra, 0)
    expect(findObject(added, 'v4')).toMatchObject({ parent: { id: 'f1' }, index: 0 })
    expect(findObject(tree, 'v4')).toBeUndefined()

    const renamed = updateObject(added, 'v4', { name: 'direction', length: 2 })
    expect(findObject(renamed, 'v4')?.node).toMatchObject({ name: 'direction', length: 2 })

    const moved = moveObject(renamed, 'v4', null, 1)
    expect(moved.objects.map((node) => node.id)).toEqual(['f1', 'v4', 'v2', 'v3'])
    expect(removeObject(moved, 'f1').objects.map((node) => node.id)).toEqual(['v4', 'v2', 'v3'])
  })

  it('refuses edits that break the tree', () => {
    expect(() => addObject(tree, null, { ...extra, id: 'v1' })).toThrow(/already in the project/)
    expect(() => addObject(tree, 'v2', extra)).toThrow(/No folder 'v2'/)
    expect(() => addObject(tree, null, { kind: 'reference', id: 'r0', name: 'floating', targetId: 'v1' })).toThrow(/References cannot exist at the top level/)
    expect(() => moveObject(tree, 'f1', 'f1')).toThrow(/into itself/)
    expect(() => updateObject(tree, 'f1', { length: 3 })).toThrow(/is a folder/)
    expect(() => removeObject(tree, 'missing')).toThrow(/No object/)
  })

  it('keeps references when their target moves and removes them with their target', () => {
    const linked = addObject(tree, 'f1', { kind: 'reference', id: 'r1', name: 'label', targetId: 'v2' })
    expect(parseProject(serializeProject(linked))).toEqual(linked)
    expect(() => moveObject(linked, 'r1', null)).toThrow(/References cannot exist at the top level/)
    const moved = moveObject(linked, 'v2', 'f1')
    expect(findObject(moved, 'r1')?.node).toMatchObject({ targetId: 'v2' })
    expect(findObject(removeObject(moved, 'v2'), 'r1')).toBeUndefined()
  })
})
