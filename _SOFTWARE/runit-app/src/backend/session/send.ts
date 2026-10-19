import type { ServiceCatalog } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import type { Request } from '../protocol/frames'
import { serviceCall } from '../protocol/services'
import { CommandError } from './commands'
import type { CommandErrorCode } from './commands'
import type { RunitLink } from './link'

/*
 * Sending a list of requests that has to arrive in order and be answered one by one: the frames that load a program
 * (OPEN, objects, values, accessors, blocks: each needs the one before). Up to `window` are on the wire at once (the board runs
 * normal requests in the order they arrived, so this saves a round trip each and reorders nothing); every answer is awaited and
 * kept. The first request that fails stops the sending: nothing after it is sent (a block would not load without its objects),
 * the ones already on the wire are awaited, and the error is thrown as it is. Loading a program with retries and a cleanup after
 * a failure is `loadProgram`.
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

/** The frames the engine makes for a program (`{ op, payload }`, the commands of the VM router; writes all). */
export interface ProgramFrame {
  readonly op: number
  readonly payload: Uint8Array
}

/** Loads (or otherwise sends) the engine's frames to the board's VM router, in order. */
export const sendProgram = (link: RunitLink, frames: readonly ProgramFrame[], options?: SendOptions): Promise<Uint8Array[]> =>
  sendInOrder(
    link,
    frames.map((frame) => ({ keys: [link.defs.keys.vm!, link.defs.dir.write, frame.op], payload: frame.payload })),
    options,
  )

/*
 * Loading a program (VM_CORE.MD "Fail closed"): a load op the board refuses discards the whole program and the upload restarts at
 * OPEN; nothing is rolled back piecemeal. So a load is retried whole, from its OPEN (which replaces whatever the board holds),
 * never from the frame that failed: one that timed out or was on the wire when the link dropped may have run, and sending it
 * again would add its objects twice. Only a failure of the link is retried; the board's refusal (`device-error`) would come
 * again. When the load gives up, RESET unloads what a broken upload left (an interrupted load stays half-loaded on the board;
 * a refused one is already gone, and RESET leaves it so).
 */

/** The VM router's commands a load needs, in its descriptor (`contract_vm.json`). */
export const LOAD_COMMANDS = {
  service: 'VM',
  open: 'OPEN',
  reset: 'RESET',
} as const

/** Failures of the link, after which the whole load is sent again. */
const RETRYABLE: ReadonlySet<CommandErrorCode> = new Set(['timeout', 'link-lost', 'send-failed'])

export interface LoadOptions extends SendOptions {
  /** Loads sent again after a failure of the link (default 2). */
  readonly retries?: number
  /** RESET when the load gives up (default true). */
  readonly reset?: boolean
  /** Before each retry: the attempt about to start (2, 3, …) and the failure that ended the one before. */
  readonly onRetry?: (attempt: number, error: unknown) => void
  readonly catalog?: ServiceCatalog
}

/** A load that gave up: the last failure (`cause`), the attempts made, and whether RESET cleaned up after it. */
export class LoadError extends Error {
  constructor(
    override readonly cause: unknown,
    readonly attempts: number,
    /** Frames answered in the last attempt. */
    readonly answered: number,
    /** `done`: RESET was answered; `failed`: it was not (`resetError` says why); `skipped`: `reset: false`. */
    readonly reset: 'done' | 'failed' | 'skipped',
    readonly resetError?: unknown,
  ) {
    super(`The program did not load (${attempts} attempt${attempts === 1 ? '' : 's'}, ${answered} frames answered in the last, RESET ${reset}): ${cause instanceof Error ? cause.message : String(cause)}`, { cause })
    this.name = 'LoadError'
  }
}

/**
 * Loads a program: the frames the engine compiled, starting with OPEN (EXEC or other frames may follow). The answers of the attempt
 * that worked, in order; `onProgress` starts again from 0 with each attempt. Throws `LoadError` when it gives up.
 */
export const loadProgram = async (link: RunitLink, frames: readonly ProgramFrame[], options: LoadOptions = {}): Promise<Uint8Array[]> => {
  const catalog = options.catalog ?? bundledServiceCatalog()
  const vm = catalog.service(LOAD_COMMANDS.service)
  const open = catalog.command(vm, LOAD_COMMANDS.open).op
  if (frames[0]?.op !== open) throw new Error(`A program load starts with ${LOAD_COMMANDS.service} ${LOAD_COMMANDS.open} (op ${open}); this one starts with op ${frames[0]?.op ?? 'nothing'}.`)
  const retries = Math.max(0, options.retries ?? 2)

  for (let attempt = 1; ; attempt++) {
    let answered = 0
    try {
      return await sendProgram(link, frames, {
        ...options,
        onProgress: (done, total) => {
          answered = done
          options.onProgress?.(done, total)
        },
      })
    } catch (error) {
      if (attempt <= retries && error instanceof CommandError && RETRYABLE.has(error.code)) {
        options.onRetry?.(attempt + 1, error)
        continue
      }
      if (options.reset === false) throw new LoadError(error, attempt, answered, 'skipped')
      try {
        await link.request(serviceCall(catalog, LOAD_COMMANDS.service, LOAD_COMMANDS.reset).request, { timeoutMs: options.timeoutMs, label: `${LOAD_COMMANDS.service} ${LOAD_COMMANDS.reset}` })
      } catch (resetError) {
        throw new LoadError(error, attempt, answered, 'failed', resetError)
      }
      throw new LoadError(error, attempt, answered, 'done')
    }
  }
}
