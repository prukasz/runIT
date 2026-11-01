import { useEffect, useMemo, useState } from 'react'
import { deviceCall, serviceCall, Watches } from '@backend'
import type { Services } from '@backend'
import { decodeOps, DEVICE_COMMANDS, lifecycleOf } from '../devices/devices'
import type { DeviceInfo, DeviceOps } from '../devices/devices'
import { readKey } from './powerDisplay'
import type { PowerReading } from './powerDisplay'

/*
 * The Live power display's values, followed while connected: each reading's command is one watch on the board (readings that share a
 * command and its arguments, a struct answer's fields, share one), read every second and sent when its answer changed (and
 * anyway every few seconds), the short view's only unless `more`. The app asks for nothing after the first request (SYS_WATCH.MD).
 */

export const POWER_REFRESH_MS = 1000

/** Each reading's value by its position in the list; undefined while unread or when its command did not answer. */
export type PowerValues = readonly unknown[]

const callKey = (r: PowerReading): string => `${readKey(r.read)}:${JSON.stringify(r.args ?? {})}`

export function usePowerLive(services: Services, readings: readonly PowerReading[], on: boolean, more: boolean): PowerValues {
  const [values, setValues] = useState<PowerValues>([])
  useEffect(() => {
    if (!on) {
      setValues((v) => (v.length === 0 ? v : []))
      return
    }
    const wanted = readings.filter((reading) => more || reading.main)
    const answers = new Map<string, unknown>()
    const publish = () =>
      setValues(
        readings.map((reading) => {
          if (!more && !reading.main) return undefined
          const answer = answers.get(callKey(reading))
          return reading.field !== undefined && answer !== null && typeof answer === 'object' ? (answer as Record<string, unknown>)[reading.field] : answer
        }),
      )
    const watches = new Watches(services.link)
    const stops: (() => Promise<void>)[] = []
    let stopped = false
    void (async () => {
      const added = new Set<string>()
      for (const reading of wanted) {
        const key = callKey(reading)
        if (added.has(key)) continue
        added.add(key)
        const r = reading.read
        const label = `${r.command} (power display)`
        const call = r.kind === 'service'
          ? serviceCall(services.catalog, r.service, r.command, reading.args, 'read')
          : deviceCall(services.link.defs, services.catalog, r.device, r.contract, r.command, reading.args, 'read')
        try {
          const stop = await watches.follow({ ...call, label }, { trigger: 'change', periodMs: POWER_REFRESH_MS, silenceMs: 3 * POWER_REFRESH_MS }, (value) => {
            answers.set(key, value)
            if (!stopped) publish()
          })
          stops.push(stop)
        } catch {
          answers.set(key, undefined) // not answered: the reading stays empty
        }
      }
      if (!stopped) publish()
    })()
    return () => {
      stopped = true
      for (const stop of stops) void stop().catch(() => undefined)
      watches.close()
    }
  }, [services, readings, on, more])
  return values
}

/**
 * The OPS answer of each device with a power contract, read once each time the link comes up: what the display may offer of it
 * (`readCommands`). A device that does not answer has none: everything its descriptors say is offered.
 */
export function usePowerOps(services: Services, devices: readonly DeviceInfo[], on: boolean): ReadonlyMap<number, DeviceOps> {
  const [ops, setOps] = useState<ReadonlyMap<number, DeviceOps>>(new Map())
  const lifecycle = useMemo(() => lifecycleOf(services.catalog), [services.catalog])
  const ids = devices.filter((d) => (d.type?.contracts ?? []).some((c) => services.catalog.contracts.get(c)?.category === 'power')).map((d) => d.id).join(',')
  useEffect(() => {
    if (!on || ids === '') {
      setOps((o) => (o.size === 0 ? o : new Map()))
      return
    }
    let stopped = false
    void (async () => {
      const next = new Map<number, DeviceOps>()
      for (const id of ids.split(',').map(Number)) {
        try {
          const { value } = await services.device(id, lifecycle.id, DEVICE_COMMANDS.ops)
          next.set(id, decodeOps(value))
        } catch {
          // not answered: the descriptors stand
        }
      }
      if (!stopped) setOps(next)
    })()
    return () => {
      stopped = true
    }
  }, [services, lifecycle, ids, on])
  return ops
}
