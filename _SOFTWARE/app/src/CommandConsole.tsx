import { useEffect, useMemo, useState } from 'react'
import type { PacketStructValues, PacketValue } from './backend/packetPack'
import { CommandError } from './backend/protocol'
import type { CommandEvent, CommandResponse } from './backend/protocol'
import type { RunitBleSession } from './backend/runitBleSession'
import { errorOwnerName, errorTagName } from './domain/decoder'
import { decodeResponseData, packCommand, runitCommandCatalog, runitErrorCatalog } from './domain/descriptors'
import type { CommandDescriptor, CommandFieldInfo } from './domain/descriptors'

const formatHex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0').toUpperCase()).join(' ')

const hexToBytes = (value: string): Uint8Array => {
  const clean = value.replaceAll('0x', '').replaceAll(/\s/g, '')
  if (!clean || clean.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(clean)) throw new Error('Enter complete hexadecimal bytes, for example: 01 24 03 04')
  return Uint8Array.from(clean.match(/../g)!, (byte) => Number.parseInt(byte, 16))
}

const isFloat = (type: string): boolean => type === 'float' || type === 'double'
const is64 = (type: string): boolean => type.endsWith('64_t')

const parseNumber = (text: string, field: CommandFieldInfo): number | bigint => {
  const clean = text.trim()
  if (isFloat(field.type)) {
    const value = Number(clean)
    if (!Number.isFinite(value)) throw new Error(`${field.label}: '${text}' is not a number.`)
    return value
  }
  if (!/^-?(0x[0-9a-f]+|\d+)$/i.test(clean)) throw new Error(`${field.label}: '${text}' is not an integer.`)
  const negative = clean.startsWith('-')
  const magnitude = BigInt(negative ? clean.slice(1) : clean)
  const value = negative ? -magnitude : magnitude
  return is64(field.type) ? value : Number(value)
}

/** Form text → packet values. Empty inputs are left out, so packCommand() sends the sentinel. */
const toValues = (command: CommandDescriptor, inputs: Readonly<Record<string, string>>): PacketStructValues => {
  const values: Record<string, PacketValue> = {}
  for (const field of command.request.fields) {
    const text = inputs[field.name] ?? ''
    if (field.kind === 'text') {
      if (text || field.required) values[field.name] = text
      continue
    }
    if (!text.trim()) continue
    if (field.kind === 'array') {
      const entries = text.split(/[\s,;]+/).filter(Boolean).map((entry) => parseNumber(entry, field))
      const length = field.arrayLength ?? entries.length
      if (entries.length > length) throw new Error(`${field.label}: at most ${length} values.`)
      values[field.name] = [...entries, ...new Array<number>(length - entries.length).fill(field.fallback)]
    } else {
      values[field.name] = parseNumber(text, field)
    }
  }
  return values
}

const formatValue = (value: PacketValue, field: CommandFieldInfo | undefined): string => {
  if (Array.isArray(value)) return `[${value.map((entry) => formatValue(entry, field)).join(', ')}]`
  if (value instanceof Uint8Array) return formatHex(value)
  if (typeof value === 'object') return JSON.stringify(value)
  const choice = field?.choices?.find((entry) => entry.value === value)
  return `${String(value)}${field?.unit ? ` ${field.unit}` : ''}${choice ? ` (${choice.alias ?? choice.symbol})` : ''}`
}

const describeResponse = (response: CommandResponse, command: CommandDescriptor | undefined): string => {
  const head = `← #${response.seq} ${response.ok ? 'OK' : 'ERROR'} ${response.receivedAt - response.sentAt} ms`
  if (!response.ok) {
    if (!response.error) return `${head}  (no tag data)`
    const errors = runitErrorCatalog()
    return `${head}  ${errorTagName(errors, response.error.tag)} @ ${errorOwnerName(errors, response.error.owner)} (details in Errors)`
  }
  if (!command?.response) return response.data.byteLength ? `${head}  data ${formatHex(response.data)}` : head
  try {
    const decoded = decodeResponseData(command, response.data)!
    const fields = command.response.fields.map((field) => `${field.label}: ${formatValue(decoded.values[field.name], field)}`).join(' · ')
    return `${head}  ${fields}${decoded.extra.byteLength ? ` · extra ${formatHex(decoded.extra)}` : ''}`
  } catch (error) {
    return `${head}  data ${formatHex(response.data)} (${error instanceof Error ? error.message : String(error)})`
  }
}

const fieldHint = (field: CommandFieldInfo): string => {
  const parts = [field.type + (field.arrayLength ? `[${field.arrayLength}]` : '')]
  if (!field.required) parts.push(`optional, default ${field.fallback}`)
  if (field.min !== undefined || field.max !== undefined) parts.push(`${field.min ?? ''}..${field.max ?? ''}`)
  if (field.unit) parts.push(field.unit)
  if (field.note) parts.push(field.note)
  return parts.join(' · ')
}

interface Props {
  readonly session?: RunitBleSession
}

/** Test-console panel: send catalog commands or raw bodies and watch their answers. */
export default function CommandConsole({ session }: Props) {
  const catalog = useMemo(() => runitCommandCatalog(), [])
  const [commandId, setCommandId] = useState(catalog.commands[0]?.id ?? '')
  const [inputs, setInputs] = useState<Record<string, string>>({})
  const [rawBody, setRawBody] = useState('')
  const [timeoutMs, setTimeoutMs] = useState('2000')
  const [log, setLog] = useState<string[]>([])
  const command = catalog.get(commandId)

  const append = (line: string): void => setLog((entries) => [line, ...entries].slice(0, 200))

  useEffect(() => {
    if (!session) return undefined
    const byHeader = new Map(catalog.commands.map((entry) => [(entry.classHeader << 8) | entry.packetHeader, entry]))
    return session.commands.observe((event: CommandEvent) => {
      if (event.type === 'sent') append(`→ #${event.seq} ${event.label ?? ''}  ${formatHex(event.frame)}`)
      else if (event.type === 'timeout') append(`✕ #${event.seq} ${event.label ?? ''} no answer (timeout)`)
      else if (event.type === 'unmatched') append(`? #${event.frame.seq} answer nobody waits for: ${event.frame.requestClass.toString(16)}/${event.frame.requestPacket.toString(16)} status ${event.frame.status}`)
      else append(describeResponse(event.response, byHeader.get((event.response.requestClass << 8) | event.response.requestPacket)))
    })
  }, [session, catalog])

  const send = (label: string, build: () => Uint8Array): void => {
    if (!session) return
    try {
      const body = build()
      const timeout = Number(timeoutMs)
      session.commands.send({ body, label, timeoutMs: Number.isFinite(timeout) && timeout > 0 ? timeout : undefined }).catch((error: unknown) => {
        // Timeouts are logged by the observer; show the other failures.
        if (!(error instanceof CommandError && error.code === 'timeout')) append(`✕ ${label}: ${error instanceof Error ? error.message : String(error)}`)
      })
    } catch (error) {
      append(`✕ ${label}: ${error instanceof Error ? error.message : String(error)}`)
    }
  }

  const selectCommand = (id: string): void => {
    setCommandId(id)
    setInputs({})
  }

  return (
    <section className="rounded border border-slate-700 bg-slate-900 p-4">
      <h2 className="font-semibold text-white">Commands</h2>
      <p className="mt-1 text-xs text-slate-500">
        {catalog.commands.length} commands from data-structures/. Sent as [seq][class][packet][payload]; answers matched by seq on stream 0x05.
        {!session && ' Connect and rediscover GATT to open the command session.'}
      </p>

      <div className="mt-3 flex flex-wrap items-center gap-2">
        <select aria-label="Command" value={commandId} onChange={(event) => selectCommand(event.target.value)}>
          {catalog.groups.map((group) => (
            <optgroup key={`${group.source}-${group.id}`} label={`${group.title} (class 0x${group.classHeader.toString(16).padStart(2, '0')})`}>
              {catalog.commands.filter((entry) => entry.group === group).map((entry) => (
                <option key={entry.id} value={entry.id}>{`0x${entry.packetHeader.toString(16).padStart(2, '0')} ${entry.name}`}</option>
              ))}
            </optgroup>
          ))}
        </select>
        <label className="flex items-center gap-1 text-xs text-slate-400">
          Timeout ms
          <input className="w-20" value={timeoutMs} onChange={(event) => setTimeoutMs(event.target.value)} />
        </label>
      </div>

      {command && (
        <div className="mt-3 space-y-2">
          {command.request.fields.map((field) => (
            <label key={field.name} className="grid gap-1 sm:grid-cols-[12rem_1fr] sm:items-center">
              <span className="text-slate-300">{field.label}{field.required ? ' *' : ''}</span>
              <span className="flex flex-col gap-1">
                {field.choices && field.kind === 'number' ? (
                  <select value={inputs[field.name] ?? ''} onChange={(event) => setInputs((values) => ({ ...values, [field.name]: event.target.value }))}>
                    <option value="">{field.required ? '— choose —' : `default (${field.fallback})`}</option>
                    {field.choices.map((choice) => (
                      <option key={choice.symbol} value={String(choice.value)} title={choice.description}>{`${choice.value} · ${choice.alias ?? choice.symbol}`}</option>
                    ))}
                  </select>
                ) : (
                  <input
                    value={inputs[field.name] ?? ''}
                    placeholder={field.kind === 'array' ? 'values separated by spaces or commas' : field.required ? '' : String(field.fallback)}
                    onChange={(event) => setInputs((values) => ({ ...values, [field.name]: event.target.value }))}
                  />
                )}
                <span className="text-xs text-slate-500">{fieldHint(field)}</span>
              </span>
            </label>
          ))}
          <button disabled={!session} onClick={() => send(command.name, () => packCommand(command, toValues(command, inputs)))}>Send {command.name}</button>
        </div>
      )}

      <div className="mt-4 flex flex-wrap items-center gap-2 border-t border-slate-800 pt-3">
        <input aria-label="Raw command body" className="min-w-64 flex-1" value={rawBody} onChange={(event) => setRawBody(event.target.value)} placeholder="raw [class][packet][payload], e.g. 01 24 03 04" />
        <button disabled={!session} onClick={() => send('raw', () => hexToBytes(rawBody))}>Send raw</button>
      </div>

      <div className="mt-3 flex items-center gap-2">
        <h3 className="text-slate-300">Command log</h3>
        <button onClick={() => setLog([])}>Clear</button>
      </div>
      <pre className="mt-2 max-h-72 overflow-auto whitespace-pre-wrap text-xs text-amber-200">{log.join('\n') || 'No commands yet.'}</pre>
    </section>
  )
}
