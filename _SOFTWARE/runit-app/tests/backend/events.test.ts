import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, decodeEvent, encodeFields, EVENT_LAYOUT } from '@backend'

const catalog = bundledServiceCatalog()
const layout = catalog.contracts.get(EVENT_LAYOUT.contract)!.commands.get(EVENT_LAYOUT.command)!.write!.args!
const C = (path: string) => readFileSync(new URL(`../../../runit-esp-rework/components/${path}`, import.meta.url), 'utf8')

/** A frame of the events stream as sys_event_stream sends it: the `sys_event_t` as it is. */
const frame = (fields: Record<string, number | Uint8Array>) => encodeFields(layout, { channel: 0, flags: 0, len: 0, t_ms: 0, data: new Uint8Array(0), ...fields })

describe('events from the board', () => {
  it('sys_event_t is the 20 bytes of sys_event.h', () => {
    expect(layout.fixed).toBe(20)
    expect(C('sys/sys_event/sys_event.h')).toMatch(/sizeof\(sys_event_t\) == 20/)
  })

  it('reads an event with its contract and its row of the events table', () => {
    const lifecycle = catalog.contracts.get('DEVICE')!
    const suspended = lifecycle.events.find((e) => e.id === 'SUSPENDED')!
    const read = decodeEvent(catalog, frame({ tag: suspended.code!, source: 3, flags: 1, t_ms: 1234 }))
    expect(read).toMatchObject({ source: 3, code: suspended.code, contract: { id: 'DEVICE' }, event: { id: 'SUSPENDED' }, flags: 1, tMs: 1234 })
    const io = catalog.contracts.get('IO')!
    expect(decodeEvent(catalog, frame({ tag: io.events.find((e) => e.id === 'FALLING')!.code!, source: 1, channel: 22 }))).toMatchObject({ event: { id: 'FALLING' }, channel: 22 })
  })

  it('an event with a payload has it read by its row', () => {
    const created = catalog.contracts.get('DEVICE')!.events.find((e) => e.id === 'CREATED')!
    const read = decodeEvent(catalog, frame({ tag: created.code!, source: 3, len: 1, data: Uint8Array.of(9) }))
    expect(read?.payload).toEqual({ gen: 9 })
  })

  it('a frame cut short is no event', () => {
    expect(decodeEvent(catalog, frame({ tag: 0, source: 3 }).subarray(0, 10))).toBeUndefined()
  })

  it('the device contract describes the events of SYS_DEVICE_EVENTS, with its codes and NOTIFY as there', () => {
    const table = C('sys/sys_device/events/sys_device_events.h')
    const rows = [...table.matchAll(/X\(P,\s*(\w+),\s*(0x[0-9A-Fa-f]+),\s*(\w+)/g)].map((m) => ({ id: m[1], code: Number(m[2]), notify: m[3] === 'SYS_EVENT_NOTIFY' }))
    const lifecycle = catalog.contracts.get('DEVICE')!
    const json = JSON.parse(C('sys/sys_device/contract_device.json')) as { events: { id: string; code: number; notify: boolean }[] }
    expect(rows.length).toBeGreaterThan(0)
    expect(json.events.map((e) => ({ id: e.id, code: e.code & 0xffff, notify: e.notify }))).toEqual(rows)
    expect(lifecycle.events.map((e) => e.id)).toEqual(rows.map((r) => r.id))
  })
})
