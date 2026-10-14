import { useCallback, useMemo, useState } from 'react'
import { CommandError, RouterClient } from '../backend'
import type { LinkState } from '../ble'
import { RunitBleLink } from '../ble'
import { enumValue, routerKey, routerOp } from '../backend/catalog'
import { decodeBleList, decodeBoardSettings, decodeConnInfo, planSettings, settingsRecordIds } from '../domain/settings'
import type { BoardSettings, RecordFrames, RouterValues, SettingsSnapshot } from '../domain/settings'

const STEP_TIMEOUT_MS = 4000
/** The board puts the GATT table live this long after APPLY (CONFIG_SYS_BLE_APPLY_DELAY_MS) and then the client has to open it again. */
const APPLY_SETTLE_MS = 700
const RECONNECT_WAIT_MS = 15000
const ACTIONS = 'ROUTER_KEY_ACTIONS'
/** The records the app keeps are in the classes below the project's (user actions, settings, devices): the project's are `sys_project`'s. */
const PROJECT_FIRST = (enumValue('action_class', 'SYS_ACTIONS_CLASS_PROJECT') << 24) >>> 0
const classOf = (id: number): number => id >>> 24
const MINE = ['SYS_ACTIONS_CLASS_USER', 'SYS_ACTIONS_CLASS_DEVICE'].map((name) => enumValue('action_class', name))

/** Resolves when the link's state satisfies `wanted` (at once if it does now), false after `ms`. */
const untilLink = (link: RunitBleLink, wanted: (state: LinkState) => boolean, ms: number): Promise<boolean> =>
  new Promise((resolve) => {
    if (wanted(link.state)) return resolve(true)
    const timer = setTimeout(() => { off(); resolve(false) }, ms)
    const off = link.onState((state) => {
      if (!wanted(state)) return
      clearTimeout(timer)
      off()
      resolve(true)
    })
  })

const isGattError = (error: unknown): boolean => error instanceof Error && !(error instanceof CommandError) && /GATT|disconnected|not connected/i.test(error.message)
const idBytes = (id: number): Uint8Array => Uint8Array.of(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff, (id >>> 24) & 0xff)
const u32 = (data: Uint8Array, at: number): number => (data[at]! | (data[at + 1]! << 8) | (data[at + 2]! << 16)) + data[at + 3]! * 0x1000000

/** What the Devices tab keeps on the board: its devices' installs and default settings, and its actions. */
export interface DeviceRecords {
  /** The records a save stores (and removes), given the ids the board lists. */
  readonly images: (stored: ReadonlySet<number>) => readonly RecordFrames[]
  /** The ids of actions on the board that a save would replace but this project never put there. */
  readonly foreign: (stored: ReadonlySet<number>) => readonly number[]
  readonly saved: () => void
  readonly synced: (records: ReadonlyMap<number, readonly Uint8Array[]>) => void
}

export type SendPhase = 'idle' | 'sending' | 'sent' | 'failed'

export interface SendStatus {
  readonly phase: SendPhase
  /** What the last save did: how many frames went, or the step the board refused and why. */
  readonly message?: string
}

/**
 * The two buttons of Project settings. Both read the board first, so nothing is sent that the board would refuse:
 *   - send: the board's live GATT table (`ble` LIST) and streams (`conn` INFO) are the baseline, so only the difference is applied,
 *     then the records are stored (`planSettings`); a record is removed only when the board lists it;
 *   - sync: the records the board lists (actions LIST) are read and put in the forms.
 */
export function useSettingsSend(link: RunitBleLink, linkState: LinkState, snapshot: SettingsSnapshot, routers: readonly RouterValues[], onSynced: (board: BoardSettings) => void, devices: DeviceRecords) {
  // Lives as long as the link (no close on unmount: StrictMode runs the cleanup once on mount and a closed client never hears an answer).
  const client = useMemo(() => new RouterClient(link), [link])
  const [status, setStatus] = useState<SendStatus>({ phase: 'idle' })

  /** One request; a GATT failure (the board changed its table, Windows dropped the handles) re-opens the link and tries once more. */
  const request = useCallback(async (keys: readonly number[], payload?: Uint8Array, many = 0): Promise<Uint8Array[]> => {
    const once = () => (many ? client.requestMany(keys, payload, many, { timeoutMs: STEP_TIMEOUT_MS }) : client.request(keys, payload, { timeoutMs: STEP_TIMEOUT_MS }).then((data) => [data]))
    try {
      return await once()
    } catch (error) {
      if (!isGattError(error)) throw error
      await link.refresh()
      return once()
    }
  }, [client, link])

  /** The ids of the records the board holds below the project's class (actions LIST, page by page). */
  const storedIds = useCallback(async (): Promise<Set<number>> => {
    const ids = new Set<number>()
    let start = 1
    for (;;) {
      const [page] = await request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_LIST')], idBytes(start))
      const listed = Math.floor((page!.length - 2) / 4)
      let last = start
      for (let index = 0; index < listed; index++) {
        last = u32(page!, 2 + index * 4)
        if (last < PROJECT_FIRST) ids.add(last)
      }
      if (listed === 0 || last >= PROJECT_FIRST) return ids
      start = last + 1
    }
  }, [request])

  /** What the board runs now: its GATT table and its streams, in editor form. */
  const live = useCallback(async (): Promise<Partial<SettingsSnapshot>> => {
    const [table] = await request([routerKey('ROUTER_KEY_BLE'), routerOp('ROUTER_KEY_BLE', 'OP_LIST')])
    const [streams] = await request([routerKey('ROUTER_KEY_CONN'), routerOp('ROUTER_KEY_CONN', 'OP_INFO')])
    return { ble: decodeBleList(table!, snapshot.ble), connectors: decodeConnInfo(streams!, snapshot.connectors) }
  }, [request, snapshot])

  const send = useCallback(async () => {
    if (linkState !== 'connected') return setStatus({ phase: 'failed', message: 'The board is not connected.' })
    setStatus({ phase: 'sending', message: 'Reading the board…' })
    let steps
    try {
      const stored = await storedIds()
      const foreign = devices.foreign(stored)
      if (foreign.length) return setStatus({ phase: 'failed', message: `The board already has action ${foreign.map((id) => id & 0xffffff).join(', ')} (not from this project): give the action another ID.` })
      steps = planSettings(snapshot, routers, await live(), stored, devices.images(stored))
    } catch (error) {
      return setStatus({ phase: 'failed', message: `Reading the board: ${error instanceof Error ? error.message : String(error)}` })
    }
    setStatus({ phase: 'sending', message: 'Sending…' })
    let sent = 0
    for (const step of steps) {
      try {
        if (step.reconnects) {
          // The board answers, then drops the link ~200 ms later to put the table live: the answer may not arrive, and a retry
          // would hit a dead link. The link comes back by itself (automatic reconnect); a board that did not drop is re-opened.
          await client.request(step.keys, step.payload, { timeoutMs: STEP_TIMEOUT_MS }).catch((error: unknown) => {
            if (error instanceof CommandError) throw error
          })
          sent++
          const dropped = await untilLink(link, (state) => state !== 'connected', APPLY_SETTLE_MS * 2)
          if (!dropped) {
            // Still connected: the old handles may be stale (Windows). A PING through `request` re-opens the link only if it fails,
            // so a link that survived the change is not dropped for nothing.
            await request([routerKey('ROUTER_KEY_SYS'), routerOp('ROUTER_KEY_SYS', 'OP_PING')]).catch(() => undefined)
          }
          if (!(await untilLink(link, (state) => state === 'connected', RECONNECT_WAIT_MS))) await link.restore().catch(() => false)
          if (!(await untilLink(link, (state) => state === 'connected', RECONNECT_WAIT_MS))) throw new Error('The board did not come back after the GATT table changed.')
          continue
        }
        await request(step.keys, step.payload)
        sent++
      } catch (error) {
        if (step.tolerant && error instanceof CommandError) continue
        // a recording left open would refuse the next save (ERR_ACTION_BUSY)
        await client.request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_ABORT')]).catch(() => undefined)
        return setStatus({ phase: 'failed', message: `${step.label}: ${error instanceof Error ? error.message : String(error)}` })
      }
    }
    devices.saved()
    setStatus({ phase: 'sent', message: `${sent} ${sent === 1 ? 'frame' : 'frames'} sent` })
  }, [client, link, request, live, storedIds, linkState, snapshot, routers, devices])

  /** Reads the records the board keeps (the ones a save writes) and puts them in the forms. */
  const sync = useCallback(async () => {
    if (linkState !== 'connected') return setStatus({ phase: 'failed', message: 'The board is not connected.' })
    setStatus({ phase: 'sending', message: 'Reading the board…' })
    const records = new Map<number, Uint8Array[]>()
    const mine = new Map<number, Uint8Array[]>()
    const routerSymbols = routers.map((router) => router.ops[0]?.routerSymbol).filter((symbol): symbol is string => !!symbol)
    try {
      const stored = await storedIds()
      for (const id of settingsRecordIds(routerSymbols).filter((entry) => stored.has(entry))) {
        const [info] = await request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_INFO')], idBytes(id))
        const count = info![2]! | (info![3]! << 8)
        if (count > 0) records.set(id, await request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_READ')], idBytes(id), count))
      }
      for (const id of [...stored].filter((entry) => MINE.includes(classOf(entry)))) {
        const [info] = await request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_INFO')], idBytes(id))
        const count = info![2]! | (info![3]! << 8)
        mine.set(id, count > 0 ? await request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_READ')], idBytes(id), count) : [])
      }
    } catch (error) {
      return setStatus({ phase: 'failed', message: `Reading the board: ${error instanceof Error ? error.message : String(error)}` })
    }
    onSynced(decodeBoardSettings(records, routerSymbols, snapshot))
    devices.synced(mine)
    const total = records.size + mine.size
    setStatus({ phase: 'sent', message: total ? `${total} stored ${total === 1 ? 'record' : 'records'} read` : 'The board holds no stored settings' })
  }, [request, storedIds, linkState, routers, snapshot, onSynced, devices])

  return { status, send, sync, connected: linkState === 'connected' }
}
