import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { ObjectNode, ObjectPath, ProgramBlock, ValueNode } from '../project'
import { compileProgram, packExec, packSubscribe } from '.'

/*
 * Blink sample: the two status LEDs on the TCA6424A (device 1, pins 22 and 23,
 * high = on; the board sets them to push-pull outputs at boot) blink in turn,
 * 500 ms per state: PERIODIC → EXPR flips `phase` → IF → an IO_SET_LEVEL per LED.
 * Written to fixtures/program-samples-led.json;
 * `.claude/skills/runit-esp/scripts/led_blink_test.py` sends it, reads both pins
 * back from the expander while it runs and saves the log.
 */

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial?: ValueNode['value']): ValueNode => ({ kind: 'value', id, name: id, type, length: 1, ...(initial ? { value: initial } : {}), mutable: true, retentive: false })
const at = (root: string): ObjectPath => ({ root })

const TCA = 1
const objects: readonly ObjectNode[] = [value('phase', 'F', [0])]

const led = (id: string, pin: number, source: string): ProgramBlock => ({
  id, type: 'IO_SET_LEVEL', inputs: [at(source)], settings: { allowed_mask: 2 ** pin, device_id: TCA, default_io_num: pin, disabled_action: 'FORCE_LOW' },
})

const blocks: readonly ProgramBlock[] = [
  { id: 'every', type: 'PERIODIC', settings: { period: 500, time_base: 'MS' } },
  { id: 'flip', type: 'EXPR', inputs: [at('phase')], outputs: ['phase'], enables: [at('every:eno')], expression: { constants: [1], code: ['const', 0, 'in', 0, '-'] } },
  { id: 'which', type: 'IF', inputs: [at('phase')] },
  led('led22', 22, 'which:q0'),
  led('led23', 23, 'which:q1'),
]

describe('program samples LED', () => {
  it('matches the saved fixture', async () => {
    const catalog = runitVmCatalog()
    const maxFrameBytes = 240
    const compiled = compileProgram({ ...createProject('led blink'), objects }, catalog, { maxFrameBytes, blocks })
    expect(compiled.diagnostics).toEqual([])
    const all = [...compiled.objects.wireIdOf.entries()].filter(([id, w]) => compiled.objects.objects[w]!.node.kind === 'value' && compiled.objects.objects[w]!.node.id === id)
    const built = {
      frames: compiled.frames.map(hex),
      counts: compiled.counts,
      arenaBytes: compiled.arenaBytes,
      subscribe: hex(packSubscribe(catalog, all.map(([, wire]) => wire), maxFrameBytes)),
      run: hex(packExec(catalog, catalog.execCommands.get('NORMAL_MODE')!)),
      blocks: compiled.blocks.blocks.map((block) => `${block.wireId} ${block.id} ${block.type}`),
      objects: all.map(([id, wire]) => ({ id, wire, type: compiled.objects.objects[wire]!.type.key })),
      leds: { device: TCA, pins: [22, 23] },
    }
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/program-samples-led.json')
  })
})
