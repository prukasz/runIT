import { describe, expect, it } from 'vitest'
import { packExec } from '../compiler'
import { runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { CanvasBlock, ObjectNode, ObjectPath, PathStep, ProjectDocument, ValueNode } from '../project'
import { planVmUpload } from '.'

/*
 * The debug build (planVmUpload with `debug`) of a small canvas, for the board:
 * every block with an ENO object and the subscription the debug view uses.
 * Written to fixtures/debug-samples.json;
 * `.claude/skills/runit-esp/scripts/debug_build_test.py` uploads it, runs it and
 * reports what each block's ENO and enables did.
 */

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, value: initial, mutable: true, retentive: false })
const folder = (id: string, ...children: ObjectNode[]): ObjectNode => ({ kind: 'folder', id, name: id, children })
const at = (root: string, ...steps: PathStep[]): ObjectPath => ({ root, steps })
const tick = at('every:eno')
let row = 0
const block = (extra: Omit<CanvasBlock, 'x' | 'y'>): CanvasBlock => ({ x: 0, y: 100 * row++, ...extra })

const objects: readonly ObjectNode[] = [
  value('n', 'F', [0]),
  value('k', 'U8', [1]),
  folder('cfg', value('gains', 'F', [1.5, 2.5, 3.5], 3)),
]

const blocks: readonly CanvasBlock[] = [
  block({ id: 'every', type: 'PERIODIC', settings: { period: 50, time_base: 'MS' } }),
  block({ id: 'cnt', type: 'EXPR', inputs: [at('n')], outputs: ['n'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } }),
  block({ id: 'saw', type: 'EXPR', inputs: [at('n')], enables: [tick], expression: { constants: [8], code: ['in', 0, 'const', 0, 'mod'] } }),
  block({ id: 'gain', type: 'EXPR', inputs: [at('saw:q0'), at('cfg', { kind: 'name', name: 'gains' }, { kind: 'dynamic', index: at('k') })], enables: [tick], expression: { code: ['in', 0, 'in', 1, '*'] } }),
  block({ id: 'high', type: 'EXPR', inputs: [at('saw:q0')], enables: [tick], expression: { constants: [3], code: ['in', 0, 'const', 0, '>'] } }),
  block({ id: 'hi', type: 'IF', inputs: [at('high:q0')] }),
  // Gated: runs on a tick only while `hi` says yes, so its EN opens and closes with the sawtooth.
  block({ id: 'gated', type: 'EXPR', inputs: [at('saw:q0')], enables: [at('hi:q0'), tick], enableMode: 'all', expression: { code: ['in', 0] } }),
  block({ id: 'off', type: 'TIMER', inputs: [at('hi:q0')], settings: { mode: 'TOF', time_base: 'MS', pt: 200 } }),
]

describe('debug build samples', () => {
  it('matches the saved fixture', async () => {
    const catalog = runitVmCatalog()
    const project: ProjectDocument = { ...createProject('debug build'), objects, canvases: [{ id: 'c1', name: 'Main', blocks }] }
    const plan = planVmUpload(project, catalog, { maxFrameBytes: 240, debug: true })
    expect(plan.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
    expect(plan.ok).toBe(true)
    const layout = plan.program.objects
    const wire = (id: string) => layout.wireIdOf.get(id)!
    const built = {
      steps: plan.steps.map((step) => ({ label: step.label, frame: hex(step.frame) })),
      counts: plan.program.counts,
      arenaBytes: plan.program.arenaBytes,
      run: hex(packExec(catalog, catalog.execCommands.get('NORMAL_MODE')!)),
      subscribed: plan.subscribed,
      objects: layout.objects.filter((placed) => placed.node.kind === 'value').map((placed) => ({ id: placed.node.id, wire: placed.wireId, type: placed.type.key, length: placed.elements })),
      blocks: [...plan.debug!.blocks.values()].map((watch) => ({
        id: watch.id,
        eno: watch.eno,
        enables: watch.enables.map((path) => wire(path.root)),
        enableMode: watch.enableMode,
        outputs: watch.outputs.map((path) => wire(path.root)),
      })),
    }
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/debug-samples.json')
  })
})
