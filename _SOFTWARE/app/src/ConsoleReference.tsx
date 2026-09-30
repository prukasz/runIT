import type { ReactNode } from 'react'

/** A project item that can be opened from a numeric firmware ID. */
export interface ConsoleReference {
  readonly label: string
  readonly open: () => void
}

export type ResolveConsoleReference = (field: string, value: number, siblings: Readonly<Record<string, unknown>>) => ConsoleReference | undefined

export function ConsoleValue({ field, value, siblings, resolveReference, children }: {
  readonly field: string
  readonly value: number
  readonly siblings: Readonly<Record<string, unknown>>
  readonly resolveReference?: ResolveConsoleReference
  readonly children?: ReactNode
}) {
  const reference = resolveReference?.(field, value, siblings)
  return reference
    ? <button type="button" className="console-reference" title={`Open ${reference.label}`} onClick={(event) => { event.preventDefault(); event.stopPropagation(); reference.open() }}>{children ?? value}<span className="console-reference-name">{reference.label}</span></button>
    : <>{children ?? value}</>
}

export function ConsoleFieldTarget({ field, value, siblings, resolveReference }: {
  readonly field: string
  readonly value: number
  readonly siblings: Readonly<Record<string, unknown>>
  readonly resolveReference?: ResolveConsoleReference
}) {
  const reference = resolveReference?.(field, value, siblings)
  return reference && <button type="button" className="console-reference" title={`Open ${reference.label}`} onClick={(event) => { event.preventDefault(); event.stopPropagation(); reference.open() }}>Open {reference.label}</button>
}

/** Link IDs in firmware text only when their field names and values are explicit. */
export function ConsoleLinkedText({ text, resolveReference, context }: { readonly text: string; readonly resolveReference?: ResolveConsoleReference; readonly context?: string }) {
  const pattern = /\b(device_id|dev_id|device|pin_num|pin|io_num|object_id|obj_id|object_idx|obj_idx|object|variable_id|variable|action_id|dynamic_action_id|static_action_id|connector_id|connector|block_idx|block|id)\s*(?:[:=#]\s*|\s+)(\d+)\b/gi
  const matches = [...text.matchAll(pattern)]
  if (!matches.length) return <>{text}</>
  const siblings = Object.fromEntries(matches.map((match) => [match[1]!.toLowerCase(), Number(match[2])]))
  const parts: ReactNode[] = []
  let offset = 0
  for (const [index, match] of matches.entries()) {
    const at = match.index
    const field = match[1]!.toLowerCase()
    parts.push(text.slice(offset, at), match[0].slice(0, -match[2]!.length), <ConsoleValue key={index} field={field === 'id' && context ? `${context}:${field}` : field} value={Number(match[2])} siblings={siblings} resolveReference={resolveReference} />)
    offset = at + match[0].length
  }
  parts.push(text.slice(offset))
  return <>{parts}</>
}
