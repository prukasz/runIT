/*
 * What a BLE platform has to give the link, and nothing more: choose a board, open the runIT service on it, write packets to
 * the request characteristic, hear packets from the notify characteristics, and say when the link is gone. Web Bluetooth is
 * one implementation (webBluetooth.ts); a native one (Android, desktop) is another. Fragmenting a frame into packets and
 * putting packets back together is the link's job (framing.ts), the same on every platform.
 */

export interface BleDeviceRef {
  readonly id: string
  readonly name?: string
}

export interface BleChannel {
  /** One packet to the request characteristic; resolves when the platform accepted it (with a response, when the board acknowledged it). */
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
  /** The chooser of the platform (a user gesture is needed); only boards that advertise the runIT service are offered. */
  requestDevice(): Promise<BleDeviceRef>
  /** Boards this app was allowed to use before: they can be opened again with no chooser. */
  knownDevices(): Promise<readonly BleDeviceRef[]>
  /**
   * Connects, finds the runIT service, subscribes to every notify characteristic in it and returns the channel. All of it
   * or none: a failure leaves nothing open.
   */
  open(device: BleDeviceRef, handlers: OpenHandlers): Promise<BleChannel>
}
