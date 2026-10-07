import { useEffect, useRef, useState } from 'react'
import { runitCommandCatalog } from './domain/descriptors'
import type { PowerSettings } from './domain/project'
import { planPowerUpload } from './domain/upload'
import { sendSteps } from './sendSteps'
import type { BleDeviceConnection } from './useBleDeviceConnection'

/*
 * Power settings -> the board, automatically. The board keeps them in its own storage and can't
 * report them, so they are sent whenever a connection opens (this is the sync) and whenever they
 * change while connected.
 */

export interface PowerSync {
  readonly message: { readonly ok: boolean; readonly text: string } | undefined
}

export function usePowerSync(connection: BleDeviceConnection, power: PowerSettings): PowerSync {
  const { session } = connection
  const [message, setMessage] = useState<PowerSync['message']>()
  // Sends are chained so a quick second change can't overtake the first.
  const queue = useRef<Promise<void>>(Promise.resolve())

  useEffect(() => {
    if (!session) {
      setMessage(undefined)
      return
    }
    const plan = planPowerUpload(runitCommandCatalog(), power)
    if (!plan.ok) {
      setMessage({ ok: false, text: plan.diagnostics[0]?.message ?? 'The power settings have errors.' })
      return
    }
    queue.current = queue.current
      .then(() => sendSteps(session, plan.steps))
      .then(() => setMessage({ ok: true, text: `Power settings sent: ${plan.steps.map((step) => step.label).join(', ')}.` }))
      .catch((error) => setMessage({ ok: false, text: `Power settings not sent: ${error instanceof Error ? error.message : String(error)}` }))
  }, [session, power])

  return { message }
}
