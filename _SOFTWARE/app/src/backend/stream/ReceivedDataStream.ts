import type { ReceivedFrame, ReceivedFrameHandler } from './types'

/** Single normalized ingress point for all transport adapters. */
export class ReceivedDataStream {
  private readonly handlers = new Set<ReceivedFrameHandler>()

  subscribe(handler: ReceivedFrameHandler): () => void {
    this.handlers.add(handler)
    return () => this.handlers.delete(handler)
  }

  publish(frame: ReceivedFrame): void {
    const immutableFrame: ReceivedFrame = { ...frame, data: frame.data.slice() }
    this.handlers.forEach((handler) => handler(immutableFrame))
  }
}
