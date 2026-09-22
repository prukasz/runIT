export class BleAdapterError extends Error {
  readonly code: 'unavailable' | 'unsupported' | 'not-connected' | 'invalid-device' | 'operation-failed'

  constructor(message: string, code: 'unavailable' | 'unsupported' | 'not-connected' | 'invalid-device' | 'operation-failed', options?: ErrorOptions) {
    super(message, options)
    this.name = 'BleAdapterError'
    this.code = code
  }
}
