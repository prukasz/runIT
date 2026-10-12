/*
 * The hardware wiring model (board-canvas-concept.md §5): what a pin is electrically, and a wire between two pins.
 * A wire is visual and electrical only (no length or gauge); nets are computed from wires.
 */

export type PinKind = 'supply_in' | 'supply_out' | 'gnd' | 'io' | 'bus'
export type PinDir = 'in' | 'out' | 'inout'

/** What one pin, connector terminal or board terminal is electrically. */
export interface PinSpec {
  readonly kind: PinKind
  readonly dir: PinDir
  readonly label: string
  /** IO modes the pin supports (BINARY, PWM, ADC, DAC); empty = not stated. */
  readonly modes: readonly string[]
  readonly mV_min?: number
  readonly mV_max?: number
  /** The named board net this terminal is on; every terminal of one net is joined on the PCB. */
  readonly net?: string
  /** A bus signal such as `i2c-sda`: two bus pins connect when their signals match. */
  readonly signal?: string
  /** The sys_io pin (device ID, pin) behind a board terminal: what a wire fills into a device's pin ref. */
  readonly sysPin?: { readonly device: number; readonly pin: number }
  /** The install field (a pin ref, e.g. `oe_pin`) a connection to this pin fills. */
  readonly cfgField?: string
  /** The device's own pin number (sys_io) this pin is, from the csv `bind_pin`: where its default mode is set. */
  readonly bindPin?: number
  readonly note?: string
}

/**
 * Which way a pin lets a signal flow: it produces (drives) it, consumes (receives) it, or either. Derived from the pin's
 * direction (the csv `dir`: out = producer, in = consumer, inout = both) and the kind: a supply output produces, a supply
 * input consumes, ground is neither (both). A link needs a producer at one end and a consumer at the other.
 */
export type PinFlow = 'producer' | 'consumer' | 'both'

export const flowOf = (spec: Pick<PinSpec, 'kind' | 'dir'>): PinFlow => (spec.kind === 'gnd' ? 'both' : spec.dir === 'out' ? 'producer' : spec.dir === 'in' ? 'consumer' : 'both')
export const canProduce = (spec: Pick<PinSpec, 'kind' | 'dir'>): boolean => flowOf(spec) !== 'consumer'
export const canConsume = (spec: Pick<PinSpec, 'kind' | 'dir'>): boolean => flowOf(spec) !== 'producer'
export const FLOW_LABEL: Readonly<Record<PinFlow, string>> = { producer: 'Producer', consumer: 'Consumer', both: 'Producer or consumer' }
export const FLOW_SHORT: Readonly<Record<PinFlow, string>> = { producer: 'producer', consumer: 'consumer', both: 'both' }

/** Which end of a wire produces: `ab` (a feeds b), `ba`, or undefined when it cannot be told (two flexible pins, two consumers). */
export function flowDirection(a: PinSpec, b: PinSpec): 'ab' | 'ba' | undefined {
  const fa = flowOf(a)
  const fb = flowOf(b)
  if (fa === 'producer' && canConsume(b)) return 'ab'
  if (fb === 'producer' && canConsume(a)) return 'ba'
  if (fa === 'both' && fb === 'consumer') return 'ab'
  if (fb === 'both' && fa === 'consumer') return 'ba'
  return undefined
}

/** A named supply or ground of the board (3V3, REG2, GND ...). */
export interface NetSpec {
  readonly kind: 'gnd' | 'supply'
  readonly label: string
  readonly mV_min?: number
  readonly mV_max?: number
  /** The net can be fed from outside (the board input): a connector pin may take it in, not only provide it. */
  readonly input?: boolean
  readonly note?: string
}

/** The runIT board is this "device"; a user device is its project ID. */
export const BOARD_REF = 'board'

export interface Endpoint {
  /** `BOARD_REF` or a user device's project ID. */
  readonly device: string
  /** A board terminal's art ID, or a device pin's name (the pins csv). */
  readonly pin: string
}

export interface Wire {
  readonly id: string
  readonly a: Endpoint
  readonly b: Endpoint
}

const SEPARATOR = '|'
export const endpointKey = (endpoint: Endpoint): string => `${endpoint.device}${SEPARATOR}${endpoint.pin}`
export const sameEndpoint = (a: Endpoint, b: Endpoint): boolean => a.device === b.device && a.pin === b.pin
export const touches = (wire: Wire, endpoint: Endpoint): boolean => sameEndpoint(wire.a, endpoint) || sameEndpoint(wire.b, endpoint)
/** The other end of a wire, seen from `endpoint`. */
export const otherEnd = (wire: Wire, endpoint: Endpoint): Endpoint => (sameEndpoint(wire.a, endpoint) ? wire.b : wire.a)

export const isSupply = (kind: PinKind): boolean => kind === 'supply_in' || kind === 'supply_out'
