import type {
  DeliveryAttempt,
  DeliveryReport,
  OutgoingMessage,
  OutgoingTransport,
  RouteBinding,
  StreamTargetId,
} from './types'

export class RouteConfigurationError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'RouteConfigurationError'
  }
}

export class RouteDeliveryError extends Error {
  readonly report: DeliveryReport

  constructor(message: string, report: DeliveryReport) {
    super(message)
    this.name = 'RouteDeliveryError'
    this.report = report
  }
}

/** Resolves logical application routes to validated, runtime-configured transport endpoints. */
export class OutgoingRouter {
  private readonly transports = new Map<string, OutgoingTransport>()
  private bindings: readonly RouteBinding[] = []
  private revision = 0

  registerTransport(transport: OutgoingTransport): () => void {
    if (this.transports.has(transport.kind)) throw new RouteConfigurationError(`Transport '${transport.kind}' is already registered.`)
    this.transports.set(transport.kind, transport)
    return () => this.transports.delete(transport.kind)
  }

  getBindings(): readonly RouteBinding[] {
    return this.bindings.map((binding) => ({ ...binding, endpoint: { ...binding.endpoint, options: binding.endpoint.options ? { ...binding.endpoint.options } : undefined } }))
  }

  getRevision(): number {
    return this.revision
  }

  /** Validate every enabled assignment first, then replace the active set atomically. */
  async replaceBindings(bindings: readonly RouteBinding[]): Promise<number> {
    this.validateShape(bindings)
    await Promise.all(bindings.filter((binding) => binding.enabled).map(async (binding) => {
      const transport = this.transports.get(binding.endpoint.transport)
      if (!transport) throw new RouteConfigurationError(`No transport is registered for '${binding.endpoint.transport}'.`)
      await transport.validateBinding(binding)
    }))
    this.bindings = bindings.map((binding) => ({ ...binding, endpoint: { ...binding.endpoint, options: binding.endpoint.options ? { ...binding.endpoint.options } : undefined } }))
    this.revision += 1
    return this.revision
  }

  async send(message: OutgoingMessage): Promise<DeliveryReport> {
    const matches = this.bindings
      .filter((binding) => binding.enabled && binding.targetId === message.targetId && binding.route === message.route)
      .sort((left, right) => left.priority - right.priority)
    if (!matches.length) throw new RouteConfigurationError(`No active outbound binding for target '${message.targetId}' and route '${message.route}'.`)

    const policy = matches[0].policy
    if (matches.some((binding) => binding.policy !== policy)) {
      throw new RouteConfigurationError(`Bindings for target '${message.targetId}' and route '${message.route}' use conflicting policies.`)
    }
    if (policy === 'single') return this.sendSingle(matches[0], message)
    if (policy === 'fanout') return this.sendFanout(matches, message)
    return this.sendFallback(matches, message)
  }

  private async sendSingle(binding: RouteBinding, message: OutgoingMessage): Promise<DeliveryReport> {
    const attempt = await this.sendBinding(binding, message)
    const report = { message, attempts: [attempt] }
    if (!attempt.success) throw new RouteDeliveryError(`Delivery through binding '${binding.id}' failed.`, report)
    return report
  }

  private async sendFanout(bindings: readonly RouteBinding[], message: OutgoingMessage): Promise<DeliveryReport> {
    const report = { message, attempts: await Promise.all(bindings.map((binding) => this.sendBinding(binding, message))) }
    if (report.attempts.some((attempt) => !attempt.success)) throw new RouteDeliveryError('One or more fanout deliveries failed.', report)
    return report
  }

  private async sendFallback(bindings: readonly RouteBinding[], message: OutgoingMessage): Promise<DeliveryReport> {
    const attempts: DeliveryAttempt[] = []
    for (const binding of bindings) {
      const attempt = await this.sendBinding(binding, message)
      attempts.push(attempt)
      if (attempt.success) return { message, attempts }
    }
    const report = { message, attempts }
    throw new RouteDeliveryError('Every fallback delivery failed.', report)
  }

  private async sendBinding(binding: RouteBinding, message: OutgoingMessage): Promise<DeliveryAttempt> {
    const transport = this.transports.get(binding.endpoint.transport)
    if (!transport) return { bindingId: binding.id, transport: binding.endpoint.transport, success: false, error: 'Transport is no longer registered.' }
    try {
      await transport.send(binding, message)
      return { bindingId: binding.id, transport: transport.kind, success: true }
    } catch (error) {
      return { bindingId: binding.id, transport: transport.kind, success: false, error: error instanceof Error ? error.message : String(error) }
    }
  }

  private validateShape(bindings: readonly RouteBinding[]): void {
    const ids = new Set<string>()
    const routes = new Map<string, string>()
    for (const binding of bindings) {
      if (!binding.id || ids.has(binding.id)) throw new RouteConfigurationError(`Binding IDs must be unique; '${binding.id}' is invalid.`)
      if (!binding.targetId || !binding.route || !binding.endpoint.transport || !binding.endpoint.address) {
        throw new RouteConfigurationError(`Binding '${binding.id}' is missing target, route, transport, or endpoint address.`)
      }
      ids.add(binding.id)
      if (!binding.enabled) continue
      const routeKey = this.routeKey(binding.targetId, binding.route)
      const existingPolicy = routes.get(routeKey)
      if (existingPolicy && existingPolicy !== binding.policy) throw new RouteConfigurationError(`Active bindings for '${routeKey}' must use one policy.`)
      routes.set(routeKey, binding.policy)
    }
  }

  private routeKey(targetId: StreamTargetId, route: string): string {
    return `${targetId}\u0000${route}`
  }
}
