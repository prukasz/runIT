/*
 * A board template (board-canvas-concept.md §9.1, §9.11): the PCB outline, its mount holes and the connectors placed on
 * it. A connector is a premade part (screw terminal, pin header, XT60, USB-C) whose pins are bound to what they carry:
 * a power net, a bus signal, or a sys_io pin the firmware installed. The template is what the board creator edits; the
 * canvas draws it and the wiring reads its terminals exactly as it reads the built-in runIT board's.
 *
 * Units are millimetres; one grid cell of the Board canvas is one millimetre.
 */

export type Rotation = 0 | 90 | 180 | 270

/** What a connector pin carries. */
export type PinBinding =
  | { readonly type: 'none' }
  /** A named board net (GND, 3V3, REG1, REG2, VS); `role` says whether the pin provides it or takes it (a power input). */
  | { readonly type: 'net'; readonly net: string; readonly role?: 'source' | 'input' }
  /** A bus signal such as `i2c-sda`, optionally the sys_io pin behind it. */
  | { readonly type: 'signal'; readonly signal: string; readonly sys?: SysPinRef }
  /** An installed sys_io pin: a provider device ID and its pin number. */
  | { readonly type: 'sys'; readonly device: number; readonly pin: number; readonly overrides?: PinOverrides }

export interface SysPinRef {
  readonly device: number
  readonly pin: number
}

/** Limits a board puts on a sys pin (an analog input behind a divider takes 20 V where the chip takes 3.3 V). */
export interface PinOverrides {
  readonly dir?: 'in' | 'out' | 'inout'
  readonly modes?: readonly string[]
  readonly mV_min?: number
  readonly mV_max?: number
}

export interface ConnectorPin {
  /** The name shown on the board for this pin (`3V3`, `SDA`, `AIN0`); the connector type's pin name when empty. */
  readonly label?: string
  readonly bind: PinBinding
}

export interface BoardConnector {
  /** Stable ID within the board (`c1`); a terminal's ID is `<id>-<pin number>`. */
  readonly id: string
  /** A key of the connector library (`screw2`, `header1x4`, `xt60`, `usbc`). */
  readonly type: string
  /** The connector's centre, in millimetres from the board's top-left corner. */
  readonly x: number
  readonly y: number
  readonly rotation: Rotation
  /** Mirrored left-right before the rotation. */
  readonly mirror: boolean
  /** A name for the whole connector (`Analog inputs`), shown in the editor. */
  readonly label?: string
  /** One per pin of the connector type, in the type's order. */
  readonly pins: readonly ConnectorPin[]
}

export interface MountHole {
  readonly x: number
  readonly y: number
}

export interface BoardTemplate {
  /** `a-z0-9_`, the library file name (`<id>.board.json`). */
  readonly id: string
  readonly name: string
  readonly width: number
  readonly height: number
  /** Corner radius of the outline. */
  readonly radius: number
  /** Mount hole diameter. */
  readonly holeDiameter: number
  readonly holes: readonly MountHole[]
  readonly connectors: readonly BoardConnector[]
}

export const BOARD_ID = /^[a-z][a-z0-9_]*$/
export const MIN_BOARD_MM = 10
export const MAX_BOARD_MM = 400

/** The terminal ID of a connector's n-th pin (1-based): the key of its wire endpoint and of its group in the art. */
export const terminalId = (connectorId: string, pinNumber: number): string => `${connectorId}-${pinNumber}`

export const newBoard = (id: string, name: string): BoardTemplate => ({ id, name, width: 80, height: 60, radius: 2, holeDiameter: 3.2, holes: cornerHoles(80, 60), connectors: [] })

/** Mount holes at the four corners, 4 mm in from each edge. */
export const cornerHoles = (width: number, height: number, inset = 4): MountHole[] => [
  { x: inset, y: inset },
  { x: width - inset, y: inset },
  { x: inset, y: height - inset },
  { x: width - inset, y: height - inset },
]

/** The next free connector ID (`c1`, `c2` ...). */
export const nextConnectorId = (board: BoardTemplate): string => {
  const taken = new Set(board.connectors.map((connector) => connector.id))
  for (let n = 1; ; n++) if (!taken.has(`c${n}`)) return `c${n}`
}

export const rotatedBy = (rotation: Rotation, quarter = 1): Rotation => ((rotation + 90 * quarter + 360) % 360) as Rotation
