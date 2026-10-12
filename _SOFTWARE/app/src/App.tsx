import { useEffect, useMemo, useState } from 'react'
import { routerKey, routerOp } from './backend/catalog'
import { PingMonitor, RunitBleLink } from './ble'
import type { LinkEvent, LinkState, PingStatus } from './ble'

const hex = (bytes: Uint8Array) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(' ')
const clock = (at: number) => new Date(at).toLocaleTimeString([], { hour12: false }) + '.' + String(at % 1000).padStart(3, '0')

const TONE: Record<LinkState, string> = {
  unavailable: 'bg-zinc-600',
  idle: 'bg-zinc-500',
  connecting: 'bg-amber-500',
  connected: 'bg-emerald-600',
  waiting: 'bg-orange-600',
}

const MEANING: Record<LinkState, string> = {
  unavailable: 'This browser has no Web Bluetooth (use Chrome, Edge or Opera on localhost or https).',
  idle: 'Not connected, and not trying to be.',
  connecting: 'Opening the link to the board.',
  connected: 'Link is up.',
  waiting: 'Link is down; waiting for the board and reconnecting by itself.',
}

interface FrameLine {
  readonly at: number
  readonly stream: string
  readonly text: string
}

export function App() {
  const link = useMemo(() => new RunitBleLink(), [])
  const monitor = useMemo(() => new PingMonitor(link), [link])
  const [state, setState] = useState<LinkState>(link.state)
  const [events, setEvents] = useState<readonly LinkEvent[]>(link.events)
  const [frames, setFrames] = useState<readonly FrameLine[]>([])
  const [ping, setPing] = useState<PingStatus>(monitor.current)
  const [auto, setAuto] = useState(link.autoReconnect)
  const [name, setName] = useState<string | undefined>()
  const [error, setError] = useState<string | null>(null)
  const [since, setSince] = useState(() => Date.now())
  const [now, setNow] = useState(() => Date.now())
  const [reconnectCount, setReconnectCount] = useState(0)

  useEffect(() => {
    const offs = [
      link.onState((s, device, reason) => {
        setState(s)
        setName(device)
        setSince(Date.now())
        setReconnectCount(link.reconnects)
        if (s === 'connected') setError(null)
        void reason
      }),
      link.onEvent(() => setEvents([...link.events])),
      link.onFrame((stream, frame) => setFrames((old) => [...old.slice(-59), { at: Date.now(), stream, text: hex(frame) }])),
      link.onProblem((stream, problem) => setFrames((old) => [...old.slice(-59), { at: Date.now(), stream, text: `framing problem: ${problem}` }])),
      monitor.onStatus(setPing),
    ]
    const clockTimer = setInterval(() => setNow(Date.now()), 1000)
    void link.restore().catch(() => undefined) // reconnect to the remembered board on load, no chooser
    return () => {
      offs.forEach((off) => off())
      clearInterval(clockTimer)
    }
  }, [link, monitor])

  const run = async (action: () => Promise<unknown>) => {
    setError(null)
    try {
      await action()
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    }
  }

  const seconds = Math.max(0, Math.round((now - since) / 1000))
  const connected = state === 'connected'
  const button = 'rounded px-3 py-1.5 text-sm font-medium bg-zinc-700 hover:bg-zinc-600 disabled:opacity-40 disabled:hover:bg-zinc-700'

  return (
    <div className="min-h-screen bg-zinc-900 text-zinc-100 p-4 sm:p-6 space-y-4 max-w-4xl mx-auto">
      <h1 className="text-xl font-semibold">runIT · BLE link test</h1>

      <section className="rounded-lg bg-zinc-800 p-4 space-y-3">
        <div className="flex flex-wrap items-center gap-3">
          <span className={`rounded-full px-3 py-1 text-sm font-semibold ${TONE[state]}`}>{state}</span>
          <span className="text-zinc-300">{name ?? 'no board'}</span>
          <span className="text-zinc-400 text-sm">for {seconds} s</span>
        </div>
        <p className="text-sm text-zinc-300">{MEANING[state]}</p>
        <p className="text-sm text-zinc-400">
          Last change: <span className="text-zinc-200">{events.filter((e) => e.text.includes(':')).at(-1)?.text ?? 'none yet'}</span>
        </p>
        {error && <p className="text-sm text-red-400">{error}</p>}
        <div className="flex flex-wrap gap-2">
          <button className={button} disabled={state === 'unavailable' || connected || state === 'connecting'} onClick={() => run(() => link.pick())}>
            Choose board…
          </button>
          <button className={button} disabled={state === 'unavailable' || connected || state === 'connecting'} onClick={() => run(async () => { if (!(await link.restore())) throw new Error('No remembered board in this browser: use Choose board… once.') })}>
            Reconnect remembered
          </button>
          <button className={button} disabled={state === 'idle' || state === 'unavailable'} onClick={() => run(() => link.disconnect())}>
            Disconnect
          </button>
          <button className={button} disabled={!connected} onClick={() => run(() => link.send(Uint8Array.of(255, 0, routerKey('ROUTER_KEY_SYS'), routerOp('ROUTER_KEY_SYS', 'OP_RESTART'))))}>
            Restart board
          </button>
          <button className={button} onClick={() => run(() => link.forget())}>
            Forget board
          </button>
          <label className="flex items-center gap-2 text-sm px-2">
            <input
              type="checkbox"
              checked={auto}
              onChange={(e) => {
                link.setAutoReconnect(e.target.checked)
                setAuto(e.target.checked)
              }}
            />
            reconnect automatically
          </label>
        </div>
      </section>

      <section className="rounded-lg bg-zinc-800 p-4 grid grid-cols-2 sm:grid-cols-5 gap-3 text-sm">
        <Stat label="Board answers" value={!connected ? '—' : ping.missed === 0 ? 'yes' : `no (${ping.missed} missed)`} bad={connected && ping.missed >= 3} />
        <Stat label="Round trip" value={ping.rttMs === undefined || !connected ? '—' : `${ping.rttMs} ms`} />
        <Stat label="Board uptime" value={ping.uptimeMs === undefined || !connected ? '—' : `${(ping.uptimeMs / 1000).toFixed(1)} s`} />
        <Stat label="Rebooted" value={connected && ping.restarted ? 'yes, just now' : 'no'} bad={connected && ping.restarted} />
        <Stat label="Reconnects" value={String(reconnectCount)} />
      </section>

      <section className="rounded-lg bg-zinc-800 p-4 space-y-2">
        <h2 className="font-medium">Why the link is in this state</h2>
        <ol className="font-mono text-xs space-y-0.5 max-h-72 overflow-y-auto flex flex-col-reverse">
          {[...events].reverse().map((e, i) => (
            <li key={i} className="text-zinc-300">
              <span className="text-zinc-500">{clock(e.at)}</span> {e.text}
            </li>
          ))}
        </ol>
      </section>

      <section className="rounded-lg bg-zinc-800 p-4 space-y-2">
        <div className="flex items-center justify-between">
          <h2 className="font-medium">Frames from the board</h2>
          <button className={button} onClick={() => setFrames([])}>
            Clear
          </button>
        </div>
        <ol className="font-mono text-xs space-y-0.5 max-h-60 overflow-y-auto flex flex-col-reverse">
          {[...frames].reverse().map((f, i) => (
            <li key={i} className="text-zinc-300">
              <span className="text-zinc-500">{clock(f.at)}</span> <span className="text-sky-400">{f.stream}</span> {f.text}
            </li>
          ))}
        </ol>
      </section>
    </div>
  )
}

function Stat({ label, value, bad }: { label: string; value: string; bad?: boolean }) {
  return (
    <div>
      <div className="text-zinc-400 text-xs">{label}</div>
      <div className={bad ? 'text-red-400 font-semibold' : 'font-semibold'}>{value}</div>
    </div>
  )
}
