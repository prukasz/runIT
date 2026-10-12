import { SerialPort } from 'serialport'
import type { FrameLink } from '../../src/backend/protocol/client'

const PREFIX = '#R:'
// eslint-disable-next-line no-control-regex
const ANSI = /\x1b\[[0-9;]*[A-Za-z]/g

const toHex = (frame: Uint8Array) => Array.from(frame, (b) => b.toString(16).padStart(2, '0')).join('')

/**
 * The board's console UART as a FrameLink (components/sys/sys_uart/SYS_UART.MD): a frame is the line `#R:<hex>`, in both directions;
 * every other line is log text (delivered as a logs stream frame). Node only: used by the hardware tests, not by the app.
 * Opening the port resets a USB-Serial-JTAG board, so `open()` waits for it to boot.
 */
export class UartLink implements FrameLink {
  private readonly port: SerialPort
  private readonly handlers = new Set<(stream: 'main' | 'logs', frame: Uint8Array) => void>()
  private pending = ''

  constructor(path: string) {
    this.port = new SerialPort({ path, baudRate: 115200, autoOpen: false })
    this.port.on('data', (chunk: Buffer) => this.feed(chunk.toString('latin1')))
  }

  open(bootMs = 3000): Promise<void> {
    return new Promise((resolve, reject) => {
      this.port.open((error) => {
        if (error) return reject(error)
        setTimeout(resolve, bootMs)
      })
    })
  }

  close(): Promise<void> {
    return new Promise((resolve) => (this.port.isOpen ? this.port.close(() => resolve()) : resolve()))
  }

  async send(frame: Uint8Array): Promise<void> {
    await new Promise<void>((resolve, reject) => this.port.write(`${PREFIX}${toHex(frame)}\n`, (e) => (e ? reject(e) : resolve())))
  }

  onFrame(handler: (stream: 'main' | 'logs', frame: Uint8Array) => void): () => void {
    this.handlers.add(handler)
    return () => this.handlers.delete(handler)
  }

  private feed(text: string): void {
    this.pending += text
    let at: number
    while ((at = this.pending.indexOf('\n')) >= 0) {
      const line = this.pending.slice(0, at).replace(/\r$/, '').replace(ANSI, '')
      this.pending = this.pending.slice(at + 1)
      this.line(line)
    }
  }

  private line(line: string): void {
    // A frame can follow a cut-off log line (the boot log is dropped mid-line while nobody reads the port).
    const at = line.lastIndexOf(PREFIX)
    if (at < 0) {
      this.emit('logs', Uint8Array.from([3, ...Buffer.from(line, 'utf-8')]))
      return
    }
    if (at > 0) this.emit('logs', Uint8Array.from([3, ...Buffer.from(line.slice(0, at), 'utf-8')]))
    const hex = line.slice(at + PREFIX.length)
    if (!/^([0-9a-fA-F]{2})+$/.test(hex)) return
    this.emit('main', Uint8Array.from(hex.match(/../g)!.map((h) => parseInt(h, 16))))
  }

  private emit(stream: 'main' | 'logs', frame: Uint8Array): void {
    this.handlers.forEach((handler) => handler(stream, frame))
  }
}
