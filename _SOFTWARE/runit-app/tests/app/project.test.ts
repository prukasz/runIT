import { describe, expect, it } from 'vitest'
import { emptyProject, parseProject, PROJECT_VERSION, projectCounts, projectFileName, serializeProject } from '../../src/app/project/projectFile'
import type { ProjectFile } from '../../src/app/project/projectFile'
import { amend, record, redo, startHistory, undo } from '../../src/app/project/history'

const sample: ProjectFile = {
  name: 'Robot arm',
  program: {
    variables: [
      { kind: 'value', id: 'speed', name: 'speed', type: 'f32', count: 1 },
      { kind: 'folder', id: 'arm', name: 'arm', children: [{ kind: 'value', id: 'angle', name: 'angle', type: 'i32', count: 3 }] },
    ],
    blocks: [{ id: 'b1', type: 'math', inputs: [], outputs: [], enables: [] }],
  },
  devices: [{ id: 4, type: 'servo', name: 'Left servo' }],
  boardDefaults: [],
  pinAliases: [{ device: 1, pin: 22, name: 'LED blue' }],
  actions: [{ id: 10, name: 'Wave', steps: [{ kind: 'device', device: 4, contract: 'servo', command: 'home' }] }],
  settings: { values: { 'LOG.SET': { level: 2, mirror_serial: true } }, tables: {}, gatt: [{ uuid: 0xee00, primary: true, characteristics: [] }] },
} as unknown as ProjectFile

describe('the project file', () => {
  it('a saved project reads back as it was', () => {
    const read = parseProject(serializeProject(sample))
    expect(read).toEqual({ ok: true, project: sample, problems: [] })
  })

  it('names what it holds: values (not folders), blocks, devices', () => {
    expect(projectCounts(sample)).toEqual({ variables: 2, blocks: 1, devices: 1, actions: 1 })
    expect(projectCounts(emptyProject())).toEqual({ variables: 0, blocks: 0, devices: 0, actions: 0 })
  })

  it('a project saved before the board settings opens with none kept', () => {
    const { settings: _, ...older } = sample
    const read = parseProject(serializeProject(older as ProjectFile))
    expect(read).toMatchObject({ ok: true, project: { settings: { values: {}, tables: {} } } })
  })

  it('a file that is not a project is a message, never an exception', () => {
    expect(parseProject('not json')).toMatchObject({ ok: false, error: expect.stringMatching(/not JSON/) })
    expect(parseProject('{"a":1}')).toMatchObject({ ok: false, error: expect.stringMatching(/not a runIT project/) })
    expect(parseProject(JSON.stringify({ format: 'runit-project', version: PROJECT_VERSION + 1 }))).toMatchObject({ ok: false, error: expect.stringMatching(/newer app/) })
    expect(parseProject(JSON.stringify({ format: 'runit-project', version: 1, program: {} }))).toMatchObject({ ok: false, error: expect.stringMatching(/no program/) })
    expect(parseProject(JSON.stringify({ format: 'runit-project', version: 1, program: { variables: [], blocks: [] }, devices: [{ id: 'x' }] }))).toMatchObject({ ok: false })
  })

  it('a project with mistakes opens and lists them', () => {
    const twice = { ...sample, program: { ...sample.program, variables: [sample.program.variables[0], sample.program.variables[0]] } }
    const read = parseProject(serializeProject(twice as ProjectFile))
    expect(read.ok).toBe(true)
    expect(read.ok && read.problems.length).toBeGreaterThan(0)
  })

  it('a file name from the project name', () => {
    expect(projectFileName('Robot arm')).toBe('Robot arm.runit.json')
    expect(projectFileName('a/b:c')).toBe('a-b-c.runit.json')
    expect(projectFileName('  ')).toBe('project.runit.json')
  })
})

describe('undo and redo', () => {
  it('undo goes back, redo forward, a new edit drops what was undone', () => {
    let h = startHistory(1)
    h = record(h, 2)
    h = record(h, 3)
    h = undo(h)
    expect(h.present).toBe(2)
    h = redo(h)
    expect(h.present).toBe(3)
    h = undo(undo(h))
    expect(h.present).toBe(1)
    h = record(h, 9)
    expect(h).toEqual({ past: [1], present: 9, future: [] })
  })

  it('an amended edit is undone together with the one it continues', () => {
    let h = record(startHistory(''), 'R')
    h = amend(h, 'Ro')
    h = amend(h, 'Rob')
    expect(h).toEqual({ past: [''], present: 'Rob', future: [] })
    expect(undo(h).present).toBe('')
  })

  it('nothing to undo or redo leaves it as it is; the same state again is no edit; the past is limited', () => {
    const h = startHistory('a')
    expect(undo(h)).toBe(h)
    expect(redo(h)).toBe(h)
    expect(record(h, 'a')).toBe(h)
    let long = startHistory(0)
    for (let i = 1; i <= 10; i++) long = record(long, i, 4)
    expect(long.past).toEqual([6, 7, 8, 9])
  })
})
