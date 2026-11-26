import { useEffect, useMemo, useState } from 'react'
import { deviceCall } from '@backend'
import { Badge, Card, Sparkline } from '@ui/components'
import type { SparkPoint } from '@ui/components'
import { LIVE_WINDOW_MS, LivePins, NO_HISTORY, trimSamples } from './livePins'
import type { PinHistory } from './livePins'
import { livePinRead, pinStateOf, PIN_START_COMMANDS, answers } from './devices'
import type { DeviceInfo, LiveRead } from './devices'
import type { DevicesState } from './useDevices'

/*
 * A pin's value live (livePins.ts): in its row of the device's pins, and larger, with its past, at the top of its panel.
 * Shown for a pin whose state lets it be read (`livePinRead`), while the board has the device.
 */

/** The drawing moves on by itself this often: a value that does not change still holds until now. */
const REDRAW_MS = 250
/** The least span of a voltage drawing: noise of a few millivolts stays flat. */
const MIN_SPAN_MV = 100

const useLivePin = (device: DeviceInfo, pin: number, state: DevicesState): { readonly read?: LiveRead; readonly history: PinHistory; readonly now: number } => {
  const io = state.catalog.contracts.get('IO')
  const modeDef = io?.commands.get(PIN_START_COMMANDS.mode)
  const hasSetMode = !device.type?.pins?.fixedModes && (!modeDef || answers(state.catalog, state.ops, io!, modeDef, 'write'))
  // Only once the board said what the device answers: until then it may not have it.
  const there = state.connected && state.live.get(device.id)?.state === 'present' && state.ops !== undefined
  const read = there ? livePinRead(state.catalog, state.ops, device, pinStateOf(device, state.board, pin, hasSetMode, state.pinKinds)) : undefined
  const command = read?.command
  const field = read?.field
  const call = useMemo(() => (command === undefined ? undefined : deviceCall(state.catalog, device.id, 'IO', command, { pin }, 'read')), [state.catalog, device.id, command, pin])
  const [history, setHistory] = useState<PinHistory>(NO_HISTORY)
  const [now, setNow] = useState(() => Date.now())
  const link = state.services.link
  useEffect(() => {
    setHistory(NO_HISTORY)
    if (!call || field === undefined) return
    const stop = LivePins.of(link).follow(call, field, (next) => { setHistory(next); setNow(Date.now()) })
    const timer = setInterval(() => setNow(Date.now()), REDRAW_MS)
    return () => {
      clearInterval(timer)
      stop()
    }
  }, [link, call, field])
  return { ...(read ? { read } : {}), history, now }
}

const volts = (mv: number): string => `${(mv / 1000).toFixed(3)} V`

/** The history as the drawing's points and range, and the least and the most of it. */
const drawn = (read: LiveRead, history: PinHistory, now: number) => {
  const samples = trimSamples(history.samples, now)
  const points: SparkPoint[] = samples.map((s) => ({ x: (s.at - (now - LIVE_WINDOW_MS)) / LIVE_WINDOW_MS, y: s.value }))
  const values = samples.flatMap((s) => (s.value === undefined ? [] : [s.value]))
  const low = values.length ? Math.min(...values) : 0
  const high = values.length ? Math.max(...values) : 0
  const bottom = Math.min(0, low)
  const range = read.kind === 'level' ? { min: 0, max: 1 } : { min: bottom, max: Math.max(high * 1.1, bottom + MIN_SPAN_MV) }
  return { points, range, low, high, any: values.length > 0, value: samples[samples.length - 1]?.value }
}

const UNREAD = 'The board could not read the pin: it may not be set up this way on the board yet (Write, or send Set mode).'

function LiveValue({ read, value, failed }: { read: LiveRead; value: number | undefined; failed?: string }) {
  if (value === undefined) return <Badge tone="neutral" size="compact" title={failed ?? UNREAD}>—</Badge>
  if (read.kind === 'level') return <Badge tone={value ? 'success' : 'neutral'} size="compact" filled={value !== 0}>{value ? 'HIGH' : 'LOW'}</Badge>
  return <span className="devices-live-value">{volts(value)}</span>
}

/** A pin's live value in its row of the pins. */
export function PinLiveCell({ device, pin, state }: { device: DeviceInfo; pin: number; state: DevicesState }) {
  const { read, history, now } = useLivePin(device, pin, state)
  if (!read) return null
  const { points, range, value } = drawn(read, history, now)
  return (
    <span className="devices-live">
      <LiveValue read={read} value={value} {...(history.failed ? { failed: history.failed } : {})} />
      <Sparkline points={points} {...range} label="The pin in the last 30 s" />
    </span>
  )
}

/** A pin's live value at the top of its panel, with its last 30 s. */
export function PinLiveCard({ device, pin, state }: { device: DeviceInfo; pin: number; state: DevicesState }) {
  const { read, history, now } = useLivePin(device, pin, state)
  if (!read) return null
  const { points, range, low, high, any, value } = drawn(read, history, now)
  return (
    <Card title={read.kind === 'level' ? 'Level' : 'Voltage'} subhead="Live, the last 30 s.">
      <div className="devices-live-now"><LiveValue read={read} value={value} {...(history.failed ? { failed: history.failed } : {})} /></div>
      <Sparkline points={points} {...range} size="panel" label="The pin in the last 30 s" />
      {read.kind === 'voltage' && <p className="devices-hint">{any ? `Lowest ${volts(low)} · highest ${volts(high)}` : 'No reading yet.'}</p>}
      {value === undefined && history.samples.length > 0 && <p className="devices-hint">{UNREAD}</p>}
      {history.failed && <p className="devices-hint">{history.failed}</p>}
    </Card>
  )
}
