import { DEFAULT_WIRE } from '@backend'
import type { BridgeWire } from '@backend'

/* The remote board as the app keeps it, and what it tells the user when it does not come. Pure. */

/** Which of the two boards the app shows: the one it is connected to, or the one behind it. */
export type Perspective = 'main' | 'remote'

export interface StoredRemote {
  /** The remote board is wanted (connected, until Disconnect). */
  readonly on: boolean
  readonly wire: BridgeWire
}

export const parseRemote = (raw: string): StoredRemote | undefined => {
  const read = JSON.parse(raw) as { on?: unknown; wire?: Partial<Record<keyof BridgeWire, unknown>> }
  const wire = read.wire ?? {}
  const number = (value: unknown, fallback: number): number => (typeof value === 'number' && Number.isInteger(value) && value >= 0 ? value : fallback)
  return {
    on: read.on === true,
    wire: { uart: number(wire.uart, DEFAULT_WIRE.uart), tx: number(wire.tx, DEFAULT_WIRE.tx), rx: number(wire.rx, DEFAULT_WIRE.rx), baud: number(wire.baud, DEFAULT_WIRE.baud) },
  }
}

/**
 * Why the remote board is not there. `heard`: packets that came from it in the last attempt. A board that is heard but opens no
 * session does not hear the main board (it greets by itself, once a second); one not heard at all is not on the line.
 */
export const remoteReason = (reason: string | undefined, heard: number, mainConnected: boolean): string | undefined => {
  if (!mainConnected) return 'Waiting for the main board.'
  if (!reason) return undefined
  if (heard > 0) {
    return `The remote board is heard but does not answer: check the wire from the main board's TX to its RX. A USB-UART bridge on that board drives its RX too: unplug its UART USB and power it another way. (${reason})`
  }
  return `Nothing comes from the remote board: check its power, both wires, the pins and the rate. (${reason})`
}
