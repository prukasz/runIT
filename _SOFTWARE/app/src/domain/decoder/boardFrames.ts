import type { ErrorCatalog, StreamCatalog, StreamInfo, ValueNames } from '../descriptors'
import { decodeErrorPacket } from './errorPacket'
import type { ErrorReport } from './errorPacket'
import { decodeLogFrame } from './logLines'
import type { LogEntry } from './logLines'

export type BoardFrame =
  | { readonly kind: 'logs'; readonly stream: StreamInfo; readonly entries: readonly LogEntry[] }
  | { readonly kind: 'errors'; readonly stream: StreamInfo; readonly report: ErrorReport }
  /** A known stream this decoder leaves to others (interface responses, telemetry). */
  | { readonly kind: 'other'; readonly stream: StreamInfo; readonly body: Uint8Array }
  /** A first byte no published stream uses. */
  | { readonly kind: 'unknown'; readonly header: number; readonly body: Uint8Array }

export interface BoardFrameCatalogs {
  readonly streams: StreamCatalog
  readonly errors: ErrorCatalog
  /** Names annotated error payload fields when given. */
  readonly names?: ValueNames
}

/** Classify one frame from the board by its stream byte (streams.generated.json) and decode logs and errors. */
export const decodeBoardFrame = (frame: Uint8Array, catalogs: BoardFrameCatalogs): BoardFrame => {
  const header = frame[0]
  const body = frame.subarray(1)
  const stream = catalogs.streams.byHeader(header)
  if (!stream) return { kind: 'unknown', header, body }
  if (stream.name === 'logs') return { kind: 'logs', stream, entries: decodeLogFrame(body) }
  if (stream.name === 'errors') return { kind: 'errors', stream, report: decodeErrorPacket(frame, catalogs.errors, catalogs.names) }
  return { kind: 'other', stream, body }
}
