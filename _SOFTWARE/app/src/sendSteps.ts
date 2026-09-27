import { runitErrorCatalog } from './domain/descriptors'
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

/**
 * Send the steps one by one, each waiting for the board's OK. Stops at the
 * first refusal: the board then holds the steps before it, not the rest.
 */
export const sendSteps = async (session: RunitBleSession, steps: readonly UploadStep[], onProgress?: (done: number, total: number) => void): Promise<void> => {
  for (const [index, step] of steps.entries()) {
    onProgress?.(index, steps.length)
    try {
      await session.commands.call({ body: step.frame, label: step.label })
    } catch (error) {
      throw new UploadStepError(`${step.label}: ${describeCommandError(error)}`, index, step, error)
    }
  }
  onProgress?.(steps.length, steps.length)
}
