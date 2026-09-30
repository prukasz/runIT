import { runitCommandCatalog, runitErrorCatalog } from './domain/descriptors'
import { errorOwnerName, errorTagName } from './domain/decoder'
import type { UploadStep } from './domain/upload'
import { CommandError } from './backend/protocol'
import type { RunitBleSession } from './backend/runitBleSession'

/** A step the board refused or never answered; `done` steps before it went through. */
export class UploadStepError extends Error {
  readonly done: number
  readonly step: UploadStep

  constructor(message: string, done: number, step: UploadStep, cause: unknown) {
    super(message, { cause })
    this.name = 'UploadStepError'
    this.done = done
    this.step = step
  }
}

/** An error as text; a board refusal named from the error catalog. */
export const describeCommandError = (error: unknown): string => {
  if (error instanceof CommandError && error.code === 'device-error' && error.response?.error) {
    const catalog = runitErrorCatalog()
    return `${errorTagName(catalog, error.response.error.tag)} (${errorOwnerName(catalog, error.response.error.owner)})`
  }
  return error instanceof Error ? error.message : String(error)
}

const BLE_APPLY = (() => {
  const command = runitCommandCatalog().get('packet_settings_ble_apply_t')
  if (!command) throw new Error('The settings catalog lacks packet_settings_ble_apply_t.')
  return command
})()

/** The board applies a BLE apply's GATT change 200 ms after answering (SYS_BLE_APPLY_DELAY_MS); the link is re-opened after that. */
const GATT_SETTLE_MS = 600

/** The frame applies staged GATT changes (settings BLE `apply`): the link is re-opened after it. */
export const changesGatt = (frame: Uint8Array): boolean => frame.length >= 2 && frame[0] === BLE_APPLY.classHeader && frame[1] === BLE_APPLY.packetHeader

/**
 * Send the steps one by one, each waiting for the board's OK. Stops at the
 * first refusal: the board then holds the steps before it, not the rest.
 * After a BLE `apply` (the board changes its GATT table) the link is re-opened.
 */
export const sendSteps = async (session: RunitBleSession, steps: readonly UploadStep[], onProgress?: (done: number, total: number) => void): Promise<void> => {
  for (const [index, step] of steps.entries()) {
    onProgress?.(index, steps.length)
    try {
      await session.commands.call({ body: step.frame, label: step.label })
      if (changesGatt(step.frame)) {
        await new Promise((resolve) => setTimeout(resolve, GATT_SETTLE_MS))
        await session.refreshGatt()
      }
    } catch (error) {
      throw new UploadStepError(`${step.label}: ${describeCommandError(error)}`, index, step, error)
    }
  }
  onProgress?.(steps.length, steps.length)
}
