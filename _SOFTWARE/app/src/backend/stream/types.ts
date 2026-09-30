export type StreamTransport = string
export type StreamRoute = string
export type StreamTargetId = string
export type RouteDeliveryPolicy = 'single' | 'fanout' | 'fallback'
export type MessageDelivery = 'reliable' | 'best-effort'

export type EndpointOption = string | number | boolean | null

/** Transport-specific endpoint details, persisted without coupling the router to a transport SDK. */
export interface StreamEndpoint {
  readonly transport: StreamTransport
  /** Transport-specific destination, for example a BLE characteristic UUID or MQTT topic. */
  readonly address: string
  readonly options?: Readonly<Record<string, EndpointOption>>
}

export interface ReceivedFrame {
  readonly data: Uint8Array
  readonly transport: StreamTransport
  readonly targetId: StreamTargetId
  readonly endpoint: string
  readonly receivedAt: number
  /** Optional logical assignment made by an inbound binding before protocol decoding. */
  readonly route?: StreamRoute
}

export interface OutgoingMessage {
  readonly targetId: StreamTargetId
  readonly route: StreamRoute
  readonly data: Uint8Array
  readonly delivery?: MessageDelivery
  readonly correlationId?: string
}

/** One editable runtime route assignment. Bindings are project/twin JSON data. */
export interface RouteBinding {
  readonly id: string
  readonly targetId: StreamTargetId
  readonly route: StreamRoute
  readonly direction: 'outbound'
  readonly enabled: boolean
  readonly priority: number
  readonly policy: RouteDeliveryPolicy
  readonly endpoint: StreamEndpoint
  /** Discovery/configuration revision under which this binding was last activated. */
  readonly generation: number
}

export interface DeliveryAttempt {
  readonly bindingId: string
  readonly transport: StreamTransport
  readonly success: boolean
  readonly error?: string
}

export interface DeliveryReport {
  readonly message: OutgoingMessage
  readonly attempts: readonly DeliveryAttempt[]
}

export type ReceivedFrameHandler = (frame: ReceivedFrame) => void

/** Implemented by BLE, Wi-Fi, MQTT, LoRa, or test-loopback bridge modules. */
export interface OutgoingTransport {
  readonly kind: StreamTransport
  validateBinding(binding: RouteBinding): Promise<void>
  send(binding: RouteBinding, message: OutgoingMessage): Promise<void>
}
