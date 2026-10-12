import { useMemo, useState } from 'react'
import { runitCommandCatalog, runitStreamCatalog } from './domain/descriptors'
import { planSettingsUpload, runitSettingsIds } from './domain/upload'
import type { SettingsState, UploadPlan } from './domain/upload'
import { sendSteps } from './sendSteps'
import type { BleDeviceConnection } from './useBleDeviceConnection'
import { BOARD_DEFAULT_SETTINGS } from './useBoardCode'
import type { BoardCodeState } from './useBoardCode'

/*
 * BLE GATT + data connector settings → the board (live, the running config).
 * The comparison base is what the board runs: after connecting, its defaults
 * plus the settings in its stored code (read by useBoardCode); after an apply
 * on this connection, what was applied. A restart drops the connection, and
 * the next one reads the board again.
 */

export interface SettingsSync {
  readonly plan: UploadPlan
  /** Commands the next apply sends (0 = the board matches the editor). */
  readonly pending: number
  /** The board's settings are still being read: the plan is against its defaults. */
  readonly reading: boolean
  readonly busy: boolean
  readonly message: { readonly ok: boolean; readonly text: string } | undefined
  apply(): Promise<boolean>
}

export function useSettingsSync(connection: BleDeviceConnection, board: BoardCodeState, target: SettingsState): SettingsSync {
  const [applied, setApplied] = useState<{ session: unknown; state: SettingsState }>()
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState<SettingsSync['message']>()
  const { session } = connection

  const from = (applied && applied.session === session ? applied.state : undefined) ?? board.baseline ?? BOARD_DEFAULT_SETTINGS
  const plan = useMemo(() => planSettingsUpload(runitCommandCatalog(), runitStreamCatalog().ble, runitSettingsIds(), from, target), [from, target])
  const reading = !!session && board.baseline === undefined

  const apply = async (): Promise<boolean> => {
    if (!session) {
      setMessage({ ok: false, text: 'Connect a runIT board first.' })
      return false
    }
    if (reading) {
      setMessage({ ok: false, text: 'Still reading what the board runs; try again in a moment.' })
      return false
    }
    if (!plan.ok) {
      setMessage({ ok: false, text: plan.diagnostics.find((entry) => entry.severity === 'error')?.message ?? 'The settings have errors.' })
      return false
    }
    setBusy(true)
    try {
      await sendSteps(session, plan.steps)
      setApplied({ session, state: target })
      setMessage({ ok: true, text: plan.steps.length ? `Applied ${plan.steps.length} change(s). Store the code to keep them after a restart.` : 'The board already matches.' })
      return true
    } catch (error) {
      setMessage({ ok: false, text: `${error instanceof Error ? error.message : String(error)} — earlier changes went through; reconnect to read the board again.` })
      return false
    } finally {
      setBusy(false)
    }
  }

  return { plan, pending: plan.steps.length, reading, busy, message: board.note && !message ? { ok: true, text: board.note } : message, apply }
}
