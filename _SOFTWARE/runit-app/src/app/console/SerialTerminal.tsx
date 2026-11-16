import { useCallback, useMemo, useRef, useState } from 'react'
import { Download, Send } from 'lucide-react'
import { Services } from '@backend'
import type { RunitLink } from '@backend'
import { Button, Card, ChoiceField, FieldError, FormGrid, FormRow, LogLine, NumberField, SegmentedSwitch, TextField } from '@ui/components'
import type { LogLevel } from '@ui/components'
import { encodeInput, LINE_ENDINGS, showBytes } from './serial'
import type { LineEnding, SerialFormat } from './serial'
import { clock } from './ErrorView'

/*
 * The serial terminal: a command typed to what hangs on a UART port of the board (a GPS, a LoRa module). A port is a device of the UART
 * contract (SYS_UART_CONTRACT.MD): Send is TRANSACT (the RX flushed, the command sent, its answer waited for, one request), Send only
 * is SEND, and Receive is RECEIVE: what the device says by itself, read when asked. The port is made in Devices, like any device.
 */

const CONTRACT = 'UART'
const WAIT_LIMIT_MS = 1000 // the longest wait the board takes in a request
const LINES_KEPT = 500

interface TerminalLine { readonly id: number; readonly at: number; readonly level: LogLevel; readonly source: string; readonly text: string }

const message = (failure: unknown): string => (failure instanceof Error ? failure.message : String(failure))

/** A UART port the terminal can talk to: a device of the board or the project. */
export interface SerialPort { readonly id: number; readonly name: string }

export function SerialTerminal({ link, connected, ports }: { link: RunitLink; connected: boolean; ports: readonly SerialPort[] }) {
  const services = useMemo(() => new Services(link), [link])
  const [chosen, setChosen] = useState<number | undefined>()
  const port = ports.some((p) => p.id === chosen) ? chosen : ports[0]?.id
  const [text, setText] = useState('')
  const [format, setFormat] = useState<SerialFormat>('text')
  const [ending, setEnding] = useState<LineEnding>('crlf')
  const [waitMs, setWaitMs] = useState(300)
  const [lines, setLines] = useState<readonly TerminalLine[]>([])
  const [error, setError] = useState<string | undefined>()
  const nextLine = useRef(0)

  const add = useCallback((level: LogLevel, source: string, line: string) => {
    const id = nextLine.current++
    setLines((old) => [...old.slice(1 - LINES_KEPT), { id, at: Date.now(), level, source, text: line }])
  }, [])

  const run = async (work: (port: number) => Promise<void>) => {
    if (port === undefined) return
    try {
      await work(port)
      setError(undefined)
    } catch (failure) { setError(message(failure)) }
  }

  const answer = (data: Uint8Array) => add(data.length > 0 ? 'I' : 'W', 'in', data.length > 0 ? showBytes(data) : `Nothing came back in ${waitMs} ms.`)

  /** The command and its answer as one request (TRANSACT). */
  const send = () => run(async (id) => {
    const data = encodeInput(text, format, ending)
    if (data.length === 0) return
    add('D', 'out', showBytes(data))
    answer((await services.device(id, CONTRACT, 'TRANSACT', { wait_ms: waitMs, data }, { side: 'write', timeoutMs: waitMs + 2000 })).data)
  })

  /** The command only (SEND): what comes back is read with Receive. */
  const sendOnly = () => run(async (id) => {
    const data = encodeInput(text, format, ending)
    if (data.length === 0) return
    add('D', 'out', showBytes(data))
    await services.device(id, CONTRACT, 'SEND', { wait_ms: 0, flags: 0, data }, { side: 'write' })
  })

  /** What the device has said since (RECEIVE). */
  const receive = () => run(async (id) => {
    answer((await services.device(id, CONTRACT, 'RECEIVE', { wait_ms: waitMs }, { side: 'read', timeoutMs: waitMs + 2000 })).data)
  })

  const choices = ports.map((p) => ({ value: p.id, label: p.name }))
  return (
    <div className="console-serial">
      <Card title="Talk to a port" help="Send waits for one answer. Send only sends, Receive reads what the device said. A port is a UART device: add one in Devices.">
        <FormGrid>
          <FormRow label="Port">
            <ChoiceField<number> aria-label="Port" value={port} choices={choices} placeholder="No UART device yet" onChange={(p) => p !== undefined && setChosen(p)} />
          </FormRow>
          <FormRow label="Format">
            <SegmentedSwitch label="Format" value={format} options={[['text', 'Text'], ['hex', 'Hex']]} onChange={setFormat} />
          </FormRow>
          {format === 'text' && (
            <FormRow label="Line ends with">
              <ChoiceField<LineEnding> aria-label="Line ends with" value={ending} choices={LINE_ENDINGS.map((e) => ({ value: e.value, label: e.label }))} onChange={(e) => e !== undefined && setEnding(e)} />
            </FormRow>
          )}
          <FormRow label="Wait for an answer (ms)" note={`The board waits at most ${WAIT_LIMIT_MS} ms, and runs no other normal command meanwhile.`}>
            <NumberField aria-label="Wait for an answer" integer min={0} max={WAIT_LIMIT_MS} value={waitMs} onCommit={setWaitMs} />
          </FormRow>
          <FormRow label="Command">
            <TextField aria-label="Command" value={text} placeholder={format === 'hex' ? 'B5 62 06 08' : 'AT+VER?'} onChange={(e) => setText(e.currentTarget.value)}
              onKeyDown={(e) => { if (e.key === 'Enter') void send() }} />
          </FormRow>
        </FormGrid>
        <div className="project-actions">
          <Button disabled={!connected || port === undefined} onClick={() => void send()}><Send aria-hidden="true" />Send</Button>
          <Button disabled={!connected || port === undefined} onClick={() => void sendOnly()}>Send only</Button>
          <Button disabled={!connected || port === undefined} onClick={() => void receive()}><Download aria-hidden="true" />Receive</Button>
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
