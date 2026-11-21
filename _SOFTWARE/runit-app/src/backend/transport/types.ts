/*
 * What a BLE platform gives the link, and nothing more: choose a board, open a service on it with the characteristics the link
 * names (one written, the others heard), write packets, hear packets, and say when the link is gone. Web Bluetooth is one
 * implementation (webBluetooth.ts); a native one (Android, desktop, the hardware tests' bleak bridge) is another. What a packet
 * is (a piece of a framed frame, a whole raw one) is the link's business, the same on every platform.
 */

export interface BleDeviceRef {
  readonly id: string
  readonly name?: string
}

/** What to open on the board: 16-bit UUIDs, all in one service. */
export interface OpenSpec {
  readonly service: number
  /** The characteristic packets are written to. */
  readonly write: number
  /** Characteristics whose notifications come to `onPacket`; the board must have them. */
  readonly notify: readonly number[]
  /** Characteristics heard when the board has them. */
  readonly optional?: readonly number[]
}

export interface BleChannel {
  /** One packet to the written characteristic; resolves when the platform took it (with a response: when the board acknowledged it). */
  write(packet: Uint8Array): Promise<void>
  /** Drops the link on purpose: `onClosed` is not called for it. */
  close(): Promise<void>
}

export interface OpenHandlers {
  /** A notification: the 16-bit UUID of the characteristic it came from, and its bytes. */
  onPacket(characteristic: number, packet: Uint8Array): void
  /** The link dropped by itself (out of range, the board restarted, the OS dropped it). Not called after `close()`. */
  onClosed(reason: string): void
}

export interface BlePlatform {
  readonly available: boolean
  /** The platform's chooser (a user gesture is needed); only boards that advertise the runIT service are offered. */
  requestDevice(): Promise<BleDeviceRef>
  /** Boards this app was allowed to use before: they can be opened again with no chooser. */
  knownDevices(): Promise<readonly BleDeviceRef[]>
  /** Drops the permission for this board: it leaves `knownDevices()` and only the chooser offers it again. */
  forget(device: BleDeviceRef): Promise<void>
  /** Connects, finds the service, subscribes to its notify characteristics and returns the channel. All of it or none: a failure leaves nothing open. */
  open(device: BleDeviceRef, spec: OpenSpec, handlers: OpenHandlers): Promise<BleChannel>
}
