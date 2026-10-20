import { useEffect, useMemo, useState } from 'react'
import type { Services } from '@backend'
import { decodeOps, DEVICE_COMMANDS, lifecycleOf } from '../devices/devices'
import type { DeviceInfo, DeviceOps } from '../devices/devices'
import { readKey } from './powerDisplay'
import type { PowerReading } from './powerDisplay'

/*
 * The Live power display's values, read every second while connected: each reading's command once a second (readings that share a
 * command and its arguments, a struct answer's fields, share one call), the short view's only unless `more`. A slow answer is not
 * stacked on: the next second waits for it.
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
    let stopped = false
    let busy = false
    const tick = async () => {
      if (busy) return
      busy = true
      try {
        const answers = new Map<string, unknown>()
        const next: unknown[] = []
        for (const reading of readings) {
          if (!more && !reading.main) {
            next.push(undefined)
            continue
          }
          const key = callKey(reading)
          if (!answers.has(key)) {
            const options = { timeoutMs: POWER_REFRESH_MS - 100, label: `${reading.read.command} (power display)` }
            try {
              const r = reading.read
              const { value } = r.kind === 'service'
                ? await services.call(r.service, r.command, reading.args, { ...options, side: 'read' })
                : await services.device(r.device, r.contract, r.command, reading.args, { ...options, side: 'read' })
              answers.set(key, value)
            } catch {
              answers.set(key, undefined)
            }
          }
          const answer = answers.get(key)
          next.push(reading.field !== undefined && answer !== null && typeof answer === 'object' ? (answer as Record<string, unknown>)[reading.field] : answer)
        }
        if (!stopped) setValues(next)
      } finally {
        busy = false
      }
    }
    void tick()
    const timer = window.setInterval(() => void tick(), POWER_REFRESH_MS)
    return () => {
      stopped = true
      window.clearInterval(timer)
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
