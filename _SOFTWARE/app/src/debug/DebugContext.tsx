import { createContext, useContext } from 'react'
import type { LiveValue } from '../domain/compiler'
import type { ObjectPath } from '../domain/project'
import type { BlockLive } from '../domain/upload'

/*
 * The debug view's state for the components that draw it: the canvas (block
 * tint, EN / ENO strips, values on pins), the details panels, the variable
 * rows and the Debug panel with its controls. Outside a running debug session
 * everything answers "nothing live".
 */

/** What the VM is doing, as the app last set it (the board does not report its mode). */
export type RunMode = 'running' | 'paused' | 'scan' | 'block'

/** Controls of the Debug panel: VM_EXEC_NORMAL_MODE, PAUSE, RESUME, SCAN_MODE + ONCE, BLOCK_MODE, NEXT. */
export type RunAction = 'run' | 'pause' | 'resume' | 'step' | 'block' | 'next'

/** How a variable is watched: `auto` = the debug build subscribes to it for the blocks, `user` = the user chose it, `off` = not subscribed (it can be). */
export type WatchState = 'auto' | 'user' | 'off'

export interface DebugView {
  /** Debug mode is on: the board runs the debug build. */
  readonly active: boolean
  /** Uploading / starting. */
  readonly busy: boolean
  /** Why debug mode could not start or stopped. */
  readonly error?: string
  /** The project changed since the debug upload: what is shown no longer matches the canvas. */
  readonly stale: boolean
  /** Live state of one block (by block ID), when debugging and the board has reported. */
  readonly block: (blockId: string) => BlockLive | undefined
  /** The block failed recently (an error packet named it). */
  readonly failed: (blockId: string) => boolean
  /** What an object (project ID) holds now. */
  readonly read: (objectId: string) => LiveValue | undefined
  /** Start debug mode (uploads the debug build and runs it), or stop it. */
  readonly toggle: () => void
  readonly clearError: () => void
  /** What the VM is doing, while debugging. */
  readonly mode?: RunMode
  /** Block mode: the block that runs on the next Next. The board does not report it, so it is counted from the first block. */
  readonly nextBlock?: string
  readonly run: (action: RunAction) => void
  /** Whether an object is in the subscription, and why; nothing when the uploaded program lacks it (it cannot be watched). */
  readonly watchState: (objectId: string) => WatchState | undefined
  /** Subscribe to an object / to what a pin reads, or stop: sent to the board at once, no upload. */
  readonly watch: (objectId: string) => void
  readonly watchPath: (path: ObjectPath) => void
  readonly unwatch: (objectId: string) => void
  /** Objects in the subscription and what one subscribe packet holds. */
  readonly subscribed: number
  readonly subscribeLimit: number
  /** Chosen objects that did not fit the packet, or other things to know about the subscription. */
  readonly notice?: string
}

export const INACTIVE_DEBUG: DebugView = {
  active: false,
  busy: false,
  stale: false,
  toggle: () => undefined,
  clearError: () => undefined,
  block: () => undefined,
  failed: () => false,
  read: () => undefined,
  run: () => undefined,
  watchState: () => undefined,
  watch: () => undefined,
  watchPath: () => undefined,
  unwatch: () => undefined,
  subscribed: 0,
  subscribeLimit: 0,
}

const DebugContext = createContext<DebugView>(INACTIVE_DEBUG)

export const DebugProvider = DebugContext.Provider

export const useDebug = (): DebugView => useContext(DebugContext)
