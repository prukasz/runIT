import { useCallback, useEffect, useState } from 'react'
import { readCode, readCodeInfos } from './boardCode'
import type { BoardCodeInfos } from './boardCode'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from './domain/descriptors'
import { decodeFrameList, decodeStoredCode } from './domain/storedCode'
import type { StoredCodeContext } from './domain/storedCode'
import { boardDefaultSettings, runitSettingsIds } from './domain/upload'
import type { SettingsState } from './domain/upload'
import type { BleDeviceConnection } from './useBleDeviceConnection'
import type { RunitBleSession } from './backend/runitBleSession'

/*
 * What the connected board holds: its stored code (info) and the settings it
 * runs since boot. After a restart the board runs its defaults plus the
 * stored code (the boot replay), so the settings part of that code is the
 * Apply baseline (app/docs/03_records_as_storage.md §6). Read once per
 * connection: the info of both slots, and the hardware config's frames (the
 * program slot holds no settings, so it is never read for this).
 */

export const storedCodeContext = (): StoredCodeContext => ({ vm: runitVmCatalog(), commands: runitCommandCatalog(), layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: runitDeviceCatalog() })

export const BOARD_DEFAULT_SETTINGS: SettingsState = boardDefaultSettings()

export interface BoardCodeState {
  readonly session?: RunitBleSession
  /** Both stored lists: hardware config and program. */
  readonly infos?: BoardCodeInfos
  /** Settings the board runs since boot (defaults + replayed code); undefined while reading. */
  readonly baseline?: SettingsState
  /** Why the baseline may not match the board, or why it couldn't be read. */
  readonly note?: string
  readonly reading: boolean
  /** Read the info and baseline again (after storing, erasing …). */
  refresh(): void
}

/** `previous`: this connection's baseline so far; storing doesn't change what the board runs, so it stays when the stored code isn't the running one. */
const readBaseline = async (session: RunitBleSession, previous?: SettingsState): Promise<{ infos?: BoardCodeInfos; baseline: SettingsState; note?: string }> => {
  let infos: BoardCodeInfos
  try {
    infos = await readCodeInfos(session)
  } catch (error) {
    return { baseline: BOARD_DEFAULT_SETTINGS, note: `No stored code info (${error instanceof Error ? error.message : String(error)}); assuming board defaults.` }
  }
  const info = infos.config
  const { replay } = info
  if (replay.state !== 1) return { infos, baseline: BOARD_DEFAULT_SETTINGS }
  if (!info.stored) return { infos, baseline: BOARD_DEFAULT_SETTINGS, note: 'The hardware config this boot replayed was erased since; the board still runs it until it restarts. Assuming board defaults.' }

  const running = replay.crc32 === info.crc32
  if (!running && previous) return { infos, baseline: previous, note: 'The stored hardware config is newer than the running one; Load restarts the board with it.' }
  const { bytes } = await readCode(session, info)
  const decoded = decodeStoredCode(decodeFrameList(bytes), BOARD_DEFAULT_SETTINGS, storedCodeContext())
  const notes: string[] = []
  if (!running) notes.push('The board runs a hardware config stored before the current one (stored without a restart); Load restarts it with the current one.')
  if (replay.failed > 0) notes.push(`The boot replay refused ${replay.failed} frame(s): the board may lack some of these settings.`)
  return { infos, baseline: decoded.settings, ...(notes.length ? { note: notes.join(' ') } : {}) }
}

export function useBoardCode(connection: BleDeviceConnection): BoardCodeState {
  const { session } = connection
  const [state, setState] = useState<Omit<BoardCodeState, 'refresh'>>({ reading: false })
  const [version, setVersion] = useState(0)

  useEffect(() => {
    if (!session) {
      setState({ reading: false })
      return undefined
    }
    let live = true
    setState((current) => ({ ...(current.session === session ? current : {}), session, reading: true }))
    readBaseline(session, state.session === session ? state.baseline : undefined)
      .then((result) => { if (live) setState({ session, reading: false, ...result }) })
      .catch((error: unknown) => {
        if (live) setState({ session, reading: false, baseline: BOARD_DEFAULT_SETTINGS, note: `Couldn't read the stored code (${error instanceof Error ? error.message : String(error)}); assuming board defaults.` })
      })
    return () => { live = false }
  }, [session, version])

  const refresh = useCallback(() => setVersion((v) => v + 1), [])
  // A state left from another session is not this board's.
  return state.session === session ? { ...state, refresh } : { reading: !!session, refresh }
}
