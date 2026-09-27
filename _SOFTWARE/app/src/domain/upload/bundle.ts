/*
 * What goes to the board, as data: an ordered list of command frames with a
 * label each, from any compiler (VM program, BLE and connector settings).
 * The project JSON stays the source of truth; the frames are an output. Saved
 * as a file only as the stored code export (`runit-code`, domain/storedCode).
 */

export interface UploadStep {
  /** Shown in logs and errors, e.g. `vm add objects 2/3`, `ble create char 0xFF10`. */
  readonly label: string
  /** `[class][packet][payload]`, without the seq byte (CommandClient adds it). */
  readonly frame: Uint8Array
}

export interface UploadDiagnostic {
  readonly severity: 'error' | 'warning'
  readonly message: string
  /** Project ID (object, service, characteristic, connector) the message is about. */
  readonly subjectId?: string
}

/** A compile result the sender understands: no errors → send `steps` in order. */
export interface UploadPlan {
  readonly ok: boolean
  readonly steps: readonly UploadStep[]
  readonly diagnostics: readonly UploadDiagnostic[]
}

export class UploadFormatError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'UploadFormatError'
  }
}

export const toHex = (bytes: Uint8Array): string => Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join(' ')

export const fromHex = (text: string): Uint8Array => {
  const tokens = text.trim().split(/\s+/).filter(Boolean)
  return Uint8Array.from(tokens, (token) => {
    if (!/^[0-9a-f]{2}$/i.test(token)) throw new UploadFormatError(`'${token}' is not a hex byte.`)
    return Number.parseInt(token, 16)
  })
}
