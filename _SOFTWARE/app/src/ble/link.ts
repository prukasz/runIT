import { Reassembler, splitFrame } from './framing'
import type { FramingProblem } from './framing'
import { toBytes, webBluetooth } from './webBluetooth'
import type { WbBluetooth, WbCharacteristic, WbDevice } from './webBluetooth'

/** The runIT service and its characteristics (components/sys/sys_ble/sys_ble.h). */
export const RUNIT_SERVICE = 0xffe0
export const RUNIT_TX = 0xffe1 // board -> app: router answers and telemetry
export const RUNIT_RX = 0xffe2 // app -> board: router requests
export const RUNIT_LOGS = 0xffe3 // board -> app: logs and errors

/** `waiting`: the link is down and the app is watching for the board to come back. */
export type LinkState = 'unavailable' | 'idle' | 'connecting' | 'connected' | 'waiting'
/** Which characteristic a frame came on. */
export type FrameStream = 'main' | 'logs'

export interface LinkOptions {
  /** Come back by itself after the link drops (default true). */
  readonly autoReconnect?: boolean
  /** Bytes per written packet, header included. Web Bluetooth hides the MTU; 185 fits a 244-byte data length. */
  readonly packetSize?: number
  /** 16-bit UUIDs of user services the board has: Web Bluetooth only shows services named at the pick. */
  readonly extraServices?: readonly number[]
  /** Where the remembered board's id is kept (default `localStorage`, when there is one). */
  readonly storage?: Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>
  /** First and longest wait between two reconnect attempts, ms (default 500 / 8000). */
  readonly retryMs?: readonly [number, number]
}

const STORE_KEY = 'runit.ble.device'
const STANDARD_SERVICES = [0x1800, 0x1801, 0x180a] as const // Generic Access, Generic Attribute, Device Information

/** One line of the link's story: what changed and why (a drop, an advertisement, a retry, a failed attempt). */
export interface LinkEvent {
  readonly at: number
  readonly state: LinkState
  readonly text: string
}

type StateHandler = (state: LinkState, device: string | undefined, reason: string) => void
type FrameHandler = (stream: FrameStream, frame: Uint8Array) => void

const message = (error: unknown): string => (error instanceof Error ? error.message : String(error))

const defaultStorage = (): LinkOptions['storage'] => {
  try {
    return globalThis.localStorage
  } catch {
    return undefined
  }
}

/**
 * A board's command channel over BLE: frames in and out of the runIT service, and the link kept up.
 *
 * - `pick()` opens the browser's chooser (a user gesture is required once) and connects.
 * - `restore()` needs no gesture: it reconnects to the board the browser already has permission for.
 * - After a drop the link waits for the board's advertisement (`watchAdvertisements`; a timed retry where
 *   the browser has none) and connects again, with no chooser. `disconnect()` ends that.
 */
export class RunitBleLink {
  private options: Required<Omit<LinkOptions, 'storage'>> & Pick<LinkOptions, 'storage'>
  private readonly stateHandlers = new Set<StateHandler>()
  private readonly frameHandlers = new Set<FrameHandler>()
  private readonly eventHandlers = new Set<(event: LinkEvent) => void>()
  private readonly history: LinkEvent[] = []
  private attempts = 0
  private recovered = 0
  private readonly problemHandlers = new Set<(stream: FrameStream, problem: FramingProblem) => void>()
  private device?: WbDevice
  private rx?: WbCharacteristic
  private notifiers: { characteristic: WbCharacteristic; listener: EventListener }[] = []
  private current: LinkState
  /** Ends the wait of the current reconnect loop. */
  private stopWaiting?: AbortController
  /** The app wants the link up (set by pick / restore, cleared by disconnect). */
  private wanted = false
  private chain: Promise<unknown> = Promise.resolve()

  constructor(options: LinkOptions = {}) {
    this.options = {
      autoReconnect: options.autoReconnect ?? true,
      packetSize: options.packetSize ?? 185,
      extraServices: options.extraServices ?? [],
      storage: options.storage ?? defaultStorage(),
      retryMs: options.retryMs ?? [500, 8000],
    }
    this.current = webBluetooth() ? 'idle' : 'unavailable'
  }

  /** How many times the link came back by itself since this object was made. */
  get reconnects(): number {
    return this.recovered
  }

  get autoReconnect(): boolean {
    return this.options.autoReconnect
  }

  setAutoReconnect(on: boolean): void {
    this.options = { ...this.options, autoReconnect: on }
    if (!on && this.current === 'waiting') {
      this.stopWaiting?.abort()
      this.set('idle', 'automatic reconnect switched off')
    }
  }

  get state(): LinkState {
    return this.current
  }

  get deviceName(): string | undefined {
    return this.device?.name
  }

  /** The last 200 events, oldest first. */
  get events(): readonly LinkEvent[] {
    return this.history
  }

  /** Every state change and every reconnect attempt, with the reason. */
  onEvent(handler: (event: LinkEvent) => void): () => void {
    this.eventHandlers.add(handler)
    return () => this.eventHandlers.delete(handler)
  }

  onState(handler: StateHandler): () => void {
    this.stateHandlers.add(handler)
    return () => this.stateHandlers.delete(handler)
  }

  onFrame(handler: FrameHandler): () => void {
    this.frameHandlers.add(handler)
    return () => this.frameHandlers.delete(handler)
  }

  /** A packet that did not fit a frame (dropped up to the next FIRST). */
  onProblem(handler: (stream: FrameStream, problem: FramingProblem) => void): () => void {
    this.problemHandlers.add(handler)
    return () => this.problemHandlers.delete(handler)
  }

  /** Chooser, then connect. Call from a click. Returns false when the person cancelled. */
  async pick(): Promise<boolean> {
    const bluetooth = this.requireBluetooth()
    let device: WbDevice
    try {
      device = await bluetooth.requestDevice({
        filters: [{ services: [RUNIT_SERVICE] }, { namePrefix: 'runit' }],
        optionalServices: [...new Set([RUNIT_SERVICE, ...this.options.extraServices, ...STANDARD_SERVICES])],
      })
    } catch (error) {
      if (error instanceof Error && error.name === 'NotFoundError') {
        this.note('chooser closed without a board')
        return false
      }
      throw error
    }
    this.wanted = true
    this.attach(device)
    this.note(`board chosen by the user: ${device.name ?? device.id}`)
    await this.open()
    return true
  }

  /**
   * Connect to the board the browser remembers, without a chooser. False when there is none (nothing picked yet, or
   * this browser cannot list permitted devices): then `pick()` is the way in. If the board is out of range the link
   * waits for it (state `waiting`) and still returns true.
   */
  async restore(): Promise<boolean> {
    const bluetooth = this.requireBluetooth()
    // The board of this session first: it needs no permission lookup, so it works after `disconnect()` in every browser.
    let device = this.device
    if (!device) {
      if (typeof bluetooth.getDevices !== 'function') {
        this.note('cannot restore: this browser cannot list permitted devices, choose the board once')
        return false
      }
      const known = await bluetooth.getDevices()
      const saved = this.options.storage?.getItem(STORE_KEY)
      const boards = known.filter((d) => d.name?.toLowerCase().startsWith('runit'))
      device = boards.find((d) => d.id === saved) ?? (boards.length === 1 ? boards[0] : undefined)
      if (!device) {
        this.note(`cannot restore: the browser lists ${known.length} permitted device(s) and none is the remembered board, choose it again`)
        return false
      }
    }
    this.wanted = true
    this.attach(device)
    this.note(`remembered board restored without a chooser: ${device.name ?? device.id}`)
    try {
      await this.open()
    } catch (error) {
      this.wait(`first connect failed (${message(error)}), waiting for the board`)
    }
    return true
  }

  /** Writes one router frame, cut into packets. */
  async send(frame: Uint8Array): Promise<void> {
    const rx = this.rx
    if (!rx || this.current !== 'connected') throw new Error('The board is not connected.')
    const packets = splitFrame(frame, this.options.packetSize)
    // Chromium runs one GATT operation at a time; the chain also keeps the packets of one frame together.
    const run = this.chain.then(async () => {
      for (const packet of packets) await rx.writeValueWithResponse(packet)
    })
    this.chain = run.catch(() => undefined)
    await run
  }

  /** Ends the link and the automatic reconnect. The browser keeps its permission, so `restore()` works later. */
  async disconnect(): Promise<void> {
    this.wanted = false
    this.stopWaiting?.abort()
    this.teardown()
    this.device?.gatt?.disconnect()
    this.set(webBluetooth() ? 'idle' : 'unavailable', 'disconnected by the user: no reconnect')
  }

  /** Disconnect and forget which board to reconnect to. */
  async forget(): Promise<void> {
    await this.disconnect()
    this.options.storage?.removeItem(STORE_KEY)
    this.device = undefined
  }

  /** After the board changed its GATT table (Windows drops every GATT object on Service Changed): re-open the link. */
  async refresh(): Promise<void> {
    this.teardown()
    this.device?.gatt?.disconnect()
    await new Promise((resolve) => setTimeout(resolve, 50))
    await this.open()
  }

  private requireBluetooth(): WbBluetooth {
    const bluetooth = webBluetooth()
    if (!bluetooth) throw new Error('Web Bluetooth is unavailable: use Chrome, Edge or Opera in a secure context.')
    return bluetooth
  }

  private attach(device: WbDevice): void {
    if (this.device && this.device !== device) this.device.removeEventListener('gattserverdisconnected', this.onDrop)
    this.device = device
    device.addEventListener('gattserverdisconnected', this.onDrop)
    try {
      this.options.storage?.setItem(STORE_KEY, device.id)
    } catch {
      // storage can be blocked: the board is then picked again next time
    }
  }

  /** `retrying`: called by the reconnect loop, which stays in charge (state `waiting` between attempts). */
  private async open(retrying = false): Promise<void> {
    const device = this.device
    if (!device?.gatt) throw new Error('This device has no GATT server.')
    if (!retrying) this.stopWaiting?.abort()
    this.set('connecting', retrying ? 'attempt to reconnect' : 'connecting')
    try {
      const server = device.gatt.connected ? device.gatt : await device.gatt.connect()
      const service = await server.getPrimaryService(RUNIT_SERVICE)
      const [tx, rx, logs] = await Promise.all([
        service.getCharacteristic(RUNIT_TX),
        service.getCharacteristic(RUNIT_RX),
        service.getCharacteristic(RUNIT_LOGS),
      ])
      this.rx = rx
      // One GATT operation at a time: the notifications are switched on in sequence.
      await this.listen(tx, 'main')
      await this.listen(logs, 'logs')
      this.attempts = 0
      this.set('connected', 'GATT open, runIT service found, notifications on')
    } catch (error) {
      this.teardown()
      if (device.gatt?.connected) device.gatt.disconnect()
      this.set(retrying ? 'waiting' : 'idle', `connect failed: ${message(error)}`)
      throw error
    }
  }

  private async listen(characteristic: WbCharacteristic, stream: FrameStream): Promise<void> {
    const joiner = new Reassembler((problem) => this.problemHandlers.forEach((handler) => handler(stream, problem)))
    const listener: EventListener = (event) => {
      const value = (event.target as WbCharacteristic).value
      if (!value) return
      const frame = joiner.push(toBytes(value))
      if (frame) this.frameHandlers.forEach((handler) => handler(stream, frame))
    }
    characteristic.addEventListener('characteristicvaluechanged', listener)
    this.notifiers.push({ characteristic, listener })
    await characteristic.startNotifications()
  }

  private teardown(): void {
    for (const { characteristic, listener } of this.notifiers) characteristic.removeEventListener('characteristicvaluechanged', listener)
    this.notifiers = []
    this.rx = undefined
  }

  private readonly onDrop = (): void => {
    this.teardown()
    if (!this.wanted) this.set('idle', 'link closed')
    else if (this.options.autoReconnect) this.wait('link lost (the board went away or the radio dropped)')
    else this.set('idle', 'link lost, automatic reconnect is off')
  }

  /** Down, and wanted back: wait for the board's advertisement, or retry on a timer, until the link is up or `disconnect()`. */
  private wait(reason: string): void {
    this.stopWaiting?.abort()
    const stop = new AbortController()
    this.stopWaiting = stop
    this.attempts = 0
    this.set('waiting', reason)
    void this.loop(stop.signal)
  }

  private async loop(signal: AbortSignal): Promise<void> {
    const device = this.device
    if (!device) return
    const [first, longest] = this.options.retryMs
    let delay = first
    while (!signal.aborted && this.wanted) {
      const why = await this.seen(device, signal, delay)
      if (signal.aborted || !this.wanted) return
      this.attempts++
      this.note(`reconnecting (attempt ${this.attempts}): ${why}`)
      try {
        await this.open(true)
        this.recovered++
        return
      } catch {
        if (signal.aborted) return
        delay = Math.min(delay * 2, longest)
        this.note(`attempt ${this.attempts} failed, next one after at most ${delay} ms or when the board advertises`)
      }
    }
  }

  /** Resolves, with the reason, when the board advertises (browsers with `watchAdvertisements`) or after `delay` ms. */
  private async seen(device: WbDevice, signal: AbortSignal, delay: number): Promise<string> {
    const timer = (ms: number, why: string) =>
      new Promise<string>((resolve) => {
        const id = setTimeout(() => resolve(why), ms)
        signal.addEventListener('abort', () => { clearTimeout(id); resolve('stopped') }, { once: true })
      })
    if (typeof device.watchAdvertisements !== 'function') return timer(delay, `timer, ${delay} ms (this browser cannot watch advertisements)`)
    const heard = new Promise<string>((resolve) => {
      device.addEventListener('advertisementreceived', () => resolve('the board advertises again'), { once: true, signal })
      signal.addEventListener('abort', () => resolve('stopped'), { once: true })
    })
    try {
      await device.watchAdvertisements({ signal })
    } catch (error) {
      return timer(delay, `timer, ${delay} ms (watching advertisements refused: ${message(error)})`)
    }
    // A board that is already advertising may have been seen before the listener was set: do not wait forever.
    const limit = Math.max(delay, 5000)
    return Promise.race([heard, timer(limit, `timer, ${limit} ms (no advertisement seen)`)])
  }

  private set(state: LinkState, reason: string): void {
    if (this.current === state) return
    this.current = state
    this.note(`${state}: ${reason}`)
    this.stateHandlers.forEach((handler) => handler(state, this.device?.name, reason))
  }

  private note(text: string): void {
    const event = { at: Date.now(), state: this.current, text }
    this.history.push(event)
    if (this.history.length > 200) this.history.shift()
    this.eventHandlers.forEach((handler) => handler(event))
  }
}
