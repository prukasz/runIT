import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, decodeEvent, encodeFields, EVENT_LAYOUT } from '@backend'
import { defs } from './fakes'

const catalog = bundledServiceCatalog()
const layout = catalog.contracts.get(EVENT_LAYOUT.contract)!.commands.get(EVENT_LAYOUT.command)!.write!.args!
const C = (path: string) => readFileSync(new URL(`../../../runit-esp/components/${path}`, import.meta.url), 'utf8')

/** A frame of the events stream as sys_event.c sends it: [event key][NOTIFY][sys_event_t]. */
const frame = (fields: Record<string, number>) => Uint8Array.from([defs.keys.event!, defs.eventNotify, ...encodeFields(layout, fields)])

describe('events from the board', () => {
  it('sys_event_t is the 18 bytes of SYS_EVENT.MD', () => {
    expect(layout.fixed).toBe(18)
    expect(C('sys/sys_event/sys_event.h')).toMatch(/#define SYS_EVENT_OP_NOTIFY 0x0*(\w+)u/)
  })

  it('reads an event with its contract and its row of the events table', () => {
    const lifecycle = catalog.byContractId.get(catalog.lifecycleContractId!)!
    const suspended = lifecycle.events.find((e) => e.id === 'SUSPENDED')!
    const read = decodeEvent(defs, catalog, frame({ source: 3, event: suspended.code!, channel: 0, flags: 1, value: 0, t_ms: 1234 }))
    expect(read).toMatchObject({ source: 3, code: suspended.code, contract: { id: lifecycle.id }, event: { id: 'SUSPENDED' }, flags: 1, tMs: 1234 })
    const io = catalog.contracts.get('IO')!
    expect(decodeEvent(defs, catalog, frame({ source: 1, event: (io.contractId! << 8) | 1, channel: 22, flags: 0, value: 0, t_ms: 0 }))?.event?.id).toBe('FALLING')
  })

  it('a frame that is not a NOTIFY event, or is cut, is not an event', () => {
    expect(decodeEvent(defs, catalog, Uint8Array.of(defs.keys.vm!, 1, 2, 3))).toBeUndefined()
    expect(decodeEvent(defs, catalog, frame({ source: 3, event: 0, channel: 0, flags: 0, value: 0, t_ms: 0 }).subarray(0, 10))).toBeUndefined()
  })

  it('the device contract describes the events of SYS_DEVICE_EVENTS in sys_device.h, NOTIFY as there', () => {
    const table = C('sys/sys_device/registry/sys_device.h').match(/#define SYS_DEVICE_EVENTS\(X\)((?:.*\\\r?\n)*.*)/)![1]!
    const rows = [...table.matchAll(/X\((\w+),\s*(0x[0-9A-Fa-f]+),\s*(\w+)\)/g)].map((m) => ({ id: m[1], code: Number(m[2]), notify: m[3] === 'SYS_EVENT_NOTIFY' }))
    const lifecycle = catalog.byContractId.get(catalog.lifecycleContractId!)!
    const json = JSON.parse(C('sys/sys_device/registry/contract_device.json')) as { events: { id: string; code: number; notify: boolean }[] }
    expect(rows.length).toBeGreaterThan(0)
    expect(json.events.map((e) => ({ id: e.id, code: e.code, notify: e.notify }))).toEqual(rows.map((r) => ({ ...r, code: (lifecycle.contractId! << 8) | r.code })))
  })
})
