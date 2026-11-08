import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { Plus, RefreshCw, Send, Trash2 } from 'lucide-react'
import { bundledLinkDefinitions, Services, Streams } from '@backend'
import type { FieldValues, RunitLink } from '@backend'
import { Button, Card, ChoiceField, FieldError, FormGrid, FormRow, LogLine, NumberField, SegmentedSwitch, Switch, TextField } from '@ui/components'
import type { LogLevel } from '@ui/components'
import { FieldsForm } from '../devices/FieldsForm'
import type { PinContext } from '../devices/FieldsForm'
import { canListen, decodeProviders, encodeInput, LINE_ENDINGS, providerLabel, rawProviders, showBytes } from './serial'
import type { LineEnding, ProviderInfo, SerialFormat } from './serial'
import { clock } from './ErrorView'

/*
 * The serial terminal: a UART port made by hand and a command typed to what hangs on it (a GPS, a LoRa module). Send is UART RAW, the
 * command and its answer as one request, or, while listening, PROVIDER SEND with what comes back arriving on an IN stream.
 */

const RX_MAX = 256 // bytes of an answer asked for
const WAIT_LIMIT_MS = 1000 // SYS_UART_RAW_WAIT_MAX_MS
const ANSWER_MARGIN_MS = 2000 // on top of the board's own wait
const LINES_KEPT = 500

interface TerminalLine { readonly id: number; readonly at: number; readonly level: LogLevel; readonly source: string; readonly text: string }

const message = (failure: unknown): string => (failure instanceof Error ? failure.message : String(failure))

export function SerialTerminal({ link, connected, context }: { link: RunitLink; connected: boolean; context: PinContext }) {
  const services = useMemo(() => new Services(link), [link])
  const createLayout = services.catalog.command(services.catalog.service('UART'), 'CREATE').write!.args!
  const [port, setPort] = useState<FieldValues>({ provider: 0x10, port: 2, baud: 115200, mode: 2, parity: 0, stop_bits: 1, rts: null, cts: null })
  const [providers, setProviders] = useState<readonly ProviderInfo[]>([])
  const [provider, setProvider] = useState<number | undefined>()
  const [text, setText] = useState('')
  const [format, setFormat] = useState<SerialFormat>('text')
  const [ending, setEnding] = useState<LineEnding>('crlf')
  const [waitMs, setWaitMs] = useState(300)
  const [lines, setLines] = useState<readonly TerminalLine[]>([])
  const [error, setError] = useState<string | undefined>()
  const [listening, setListening] = useState(false)
  const stopListening = useRef<(() => Promise<void>) | undefined>(undefined)
  const nextLine = useRef(0)

  const add = useCallback((level: LogLevel, source: string, line: string) => {
    const id = nextLine.current++
    setLines((old) => [...old.slice(1 - LINES_KEPT), { id, at: Date.now(), level, source, text: line }])
  }, [])

  const refresh = useCallback(async () => {
    try {
      const { data } = await services.call('PROVIDER', 'LIST')
      const raw = rawProviders(decodeProviders(data))
      setProviders(raw)
      setProvider((old) => (raw.some((p) => p.id === old) ? old : raw[0]?.id))
      setError(undefined)
    } catch (failure) { setError(message(failure)) }
  }, [services])

  useEffect(() => { if (connected) void refresh() }, [connected, refresh])
  // Leaving the view stops listening: the stream it made goes with it.
  useEffect(() => () => { void stopListening.current?.().catch(() => undefined) }, [])

  const run = async (work: () => Promise<void>) => {
    try {
      await work()
      setError(undefined)
    } catch (failure) { setError(message(failure)) }
  }

  const create = () => run(async () => {
    await services.call('UART', 'CREATE', port, { side: 'write' })
    await refresh()
    setProvider(Number(port.provider))
  })

  const remove = () => provider !== undefined && run(async () => {
    await services.call('PROVIDER', 'REMOVE', { provider })
    await refresh()
  })

  /** An IN stream on the provider, this client subscribed: what the device says by itself (a GPS's sentences). */
  const listen = async (on: boolean) => {
    if (!on) {
      await run(async () => { await stopListening.current?.() })
      stopListening.current = undefined
      setListening(false)
      return
    }
    if (provider === undefined) return
    await run(async () => {
      const streams = new Streams(link, services)
      const taken = new Set((await streams.list()).map((s) => s.id))
      const defs = bundledLinkDefinitions().stream
      let id = defs.last
      while (taken.has(id) && id > defs.userFirst) id--
      await streams.create(id, { dir: 'in' })
      await streams.bind(id, provider)
      const unsubscribe = await streams.subscribe(id, (data) => add('I', 'in', showBytes(data)))
      stopListening.current = async () => {
        await unsubscribe()
        await streams.remove(id)
        streams.close()
      }
      setListening(true)
    })
  }

  const send = () => provider !== undefined && run(async () => {
    const data = encodeInput(text, format, ending)
    if (data.length === 0) return
    add('D', 'out', showBytes(data))
    if (listening) {
      await services.call('PROVIDER', 'SEND', { provider, data })
      return
    }
    const answer = await services.call('UART', 'RAW', { provider, wait_ms: waitMs, rx_max: RX_MAX, data }, { timeoutMs: waitMs + ANSWER_MARGIN_MS })
    add(answer.data.length > 0 ? 'I' : 'W', 'in', answer.data.length > 0 ? showBytes(answer.data) : `Nothing came back in ${waitMs} ms.`)
  })

  const choices = providers.map((p) => ({ value: p.id, label: providerLabel(p) }))
  return (
    <div className="console-serial">
      <Card title="Make a port" help="A free UART of the board as a raw port. The console's UART and the board's link are not free.">
        <FieldsForm layout={createLayout} values={port} onChange={setPort} context={context} />
        <div className="project-actions">
          <Button disabled={!connected} onClick={() => void create()}><Plus aria-hidden="true" />Make the port</Button>
        </div>
      </Card>
      <Card title="Talk to it" help="Send waits for one answer. Listen shows everything the device says, also by itself."
        actions={<Button size="sm" disabled={!connected} title="Read the board's raw ports again" onClick={() => void refresh()}><RefreshCw aria-hidden="true" />Refresh</Button>}>
        <FormGrid>
          <FormRow label="Port">
            <ChoiceField<number> aria-label="Port" value={provider} choices={choices} disabled={listening} placeholder="No raw port yet" onChange={(p) => p !== undefined && setProvider(p)} />
          </FormRow>
          <FormRow label="Listen" note="While listening, what comes in shows below at once and Send waits for nothing. A direct port can't: its driver reads it.">
            <Switch label={listening ? 'On' : 'Off'} checked={listening} disabled={!connected || !canListen(providers.find((p) => p.id === provider))} onChange={(on) => void listen(on)} />
          </FormRow>
          <FormRow label="Format">
            <SegmentedSwitch label="Format" value={format} options={[['text', 'Text'], ['hex', 'Hex']]} onChange={setFormat} />
          </FormRow>
          {format === 'text' && (
            <FormRow label="Line ends with">
              <ChoiceField<LineEnding> aria-label="Line ends with" value={ending} choices={LINE_ENDINGS.map((e) => ({ value: e.value, label: e.label }))} onChange={(e) => e !== undefined && setEnding(e)} />
            </FormRow>
          )}
          {!listening && (
            <FormRow label="Wait for an answer (ms)" note={`The board waits at most ${WAIT_LIMIT_MS} ms, and runs no other normal command meanwhile.`}>
              <NumberField aria-label="Wait for an answer" integer min={0} max={WAIT_LIMIT_MS} value={waitMs} onCommit={setWaitMs} />
            </FormRow>
          )}
          <FormRow label="Command">
            <TextField aria-label="Command" value={text} placeholder={format === 'hex' ? 'B5 62 06 08' : 'AT+VER?'} onChange={(e) => setText(e.currentTarget.value)}
              onKeyDown={(e) => { if (e.key === 'Enter') void send() }} />
          </FormRow>
        </FormGrid>
        <div className="project-actions">
          <Button disabled={!connected || provider === undefined} onClick={() => void send()}><Send aria-hidden="true" />Send</Button>
          <Button disabled={!connected || provider === undefined || listening} title="Remove this port" onClick={() => void remove()}><Trash2 aria-hidden="true" />Remove the port</Button>
          <Button disabled={lines.length === 0} onClick={() => setLines([])}>Clear</Button>
        </div>
        <FieldError>{error}</FieldError>
      </Card>
      <div className="console-serial-lines">
        {lines.map((l) => <LogLine key={l.id} time={clock(l.at)} level={l.level} source={l.source} text={l.text} />)}
      </div>
    </div>
  )
}
