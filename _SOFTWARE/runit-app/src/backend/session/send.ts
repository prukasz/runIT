import type { Request } from '../protocol/frames'
import type { RunitLink } from './link'

/*
 * Sending a list of requests that has to arrive in order and be answered one by one: the frames that load a program
 * (OPEN, objects, values, accessors, blocks: each needs the one before). Up to `window` are on the wire at once (the board runs
 * normal requests in the order they arrived, so this saves a round trip each and reorders nothing); every answer is awaited and
 * kept. The first request that fails stops the sending: nothing after it is sent (a block would not load without its objects),
 * the ones already on the wire are awaited, and the error is thrown as it is. Retrying and cleaning up after a failure are not
 * here yet.
 */

export interface SendOptions {
  /** Requests on the wire at once (default 4, the link's own window). */
  readonly window?: number
  /** Wait for each answer (the link's default when omitted). */
  readonly timeoutMs?: number
  /** After each answer: how many are answered, of how many. */
  readonly onProgress?: (done: number, total: number) => void
}

/** The answers of the requests, in the order of the requests. */
export const sendInOrder = async (link: RunitLink, requests: readonly Request[], options: SendOptions = {}): Promise<Uint8Array[]> => {
  const total = requests.length
  const answers: Uint8Array[] = new Array(total)
  let next = 0
  let done = 0
  let failed = false
  let failure: unknown

  const lane = async (): Promise<void> => {
    while (!failed && next < total) {
      const index = next++
      try {
        answers[index] = await link.request(requests[index]!, { timeoutMs: options.timeoutMs, label: `${index + 1}/${total}` })
        options.onProgress?.(++done, total)
      } catch (error) {
        if (!failed) {
          failed = true
          failure = error
        }
        return
      }
    }
  }

  await Promise.all(Array.from({ length: Math.min(Math.max(1, options.window ?? 4), total) }, lane))
  if (failed) throw failure
  return answers
}

/** The frames the engine makes for a program (`{ op, payload }`, the commands of the VM router). */
export interface ProgramFrame {
  readonly op: number
  readonly payload: Uint8Array
}

/** Loads (or otherwise sends) the engine's frames to the board's VM router, in order. */
export const sendProgram = (link: RunitLink, frames: readonly ProgramFrame[], options?: SendOptions): Promise<Uint8Array[]> =>
  sendInOrder(
    link,
    frames.map((frame) => ({ keys: [link.defs.keys.vm!, frame.op], payload: frame.payload })),
    options,
  )
