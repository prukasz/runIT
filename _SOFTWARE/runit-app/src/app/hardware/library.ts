import type { BoardDescriptor } from '@backend'

/*
 * The hardware library (_SOFTWARE/library/parts/<id>/: part.json and its drawing part.svg; templates/part.schema.json,
 * library/LIBRARY.md): the boards, modules, chips, actuators and passive parts of the Devices canvas. Pure: the types of a part
 * and the parts bundled with the app. A part the user makes in the creator travels in the project (`HardwareLayout.parts`) until
 * it is saved into the library.
 */

export interface Point {
  readonly x: number
  readonly y: number
}

export interface Range {
  readonly min?: number
  readonly max?: number
  readonly nominal?: number
  readonly step?: number
}

export type NetRole = 'sink' | 'source' | 'through' | 'internal'

export interface PartNet {
  readonly id: string
  readonly label: string
  readonly role: NetRole
  readonly voltage_mV?: Range
  readonly current_mA?: { readonly typical?: number; readonly peak?: number }
  readonly max_mA?: number
  readonly monitor?: { readonly device: number; readonly channel: number }
}

export type BusType = 'i2c' | 'spi' | 'uart' | 'usb'

/** One pin of a connector: at most one of the role fields says what it is (none: not connected to anything yet). */
export interface PartPin {
  readonly id: string
  readonly label: string
  readonly description?: string
  readonly at?: Point
  readonly color?: string
  readonly gnd?: true
  readonly net?: string
  readonly device_pin?: { readonly device: number | 'self'; readonly pin: number }
  readonly config?: string
  readonly bus?: { readonly type: BusType; readonly line: string; readonly bus?: number }
  readonly hbridge?: { readonly device: number; readonly channel: number; readonly supply?: string }
  readonly motor?: 'a' | 'b'
  readonly nc?: true
}

export type ConnectorType = 'goldpin' | 'goldpin_female' | 'screw_terminal' | 'wire' | 'xt60' | 'usb_c' | 'pad'
export type Rotation = 0 | 90 | 180 | 270

export interface Connector {
  readonly id: string
  readonly label?: string
  readonly type: ConnectorType
  readonly at: Point
  readonly rotation?: Rotation
  readonly pitch_mm?: number
  readonly rows?: number
  readonly from?: Point
  readonly pins: readonly PartPin[]
}

export type ElementKind = 'source' | 'fuse' | 'regulator' | 'mux' | 'load'

export interface PowerElement {
  readonly id: string
  readonly kind: ElementKind
  readonly label: string
  readonly in?: string | readonly string[]
  readonly out?: string
  readonly device?: number
  readonly voltage_mV?: Range
  readonly levels_mV?: readonly number[]
  readonly adjustable?: boolean
  readonly max_mA?: number
  readonly efficiency_pct?: number
  readonly current_mA?: { readonly typical?: number; readonly peak?: number }
  readonly inputs?: readonly string[]
  readonly ok_pin?: { readonly device: number; readonly pin: number }
  readonly description?: string
}

export type PartCategory = 'board' | 'module' | 'chip' | 'actuator' | 'sensor' | 'passive' | 'power'

export interface ChipPlacement {
  readonly id: string
  readonly label: string
  readonly type?: string
  readonly device?: number
  readonly at: Point
  readonly size_mm: { readonly w: number; readonly h: number }
}

/** A part as part.json has it. */
export interface Part {
  readonly id: string
  readonly name: string
  readonly category: PartCategory
  readonly description?: string
  readonly tags?: readonly string[]
  readonly board?: { readonly name: string; readonly revision: number }
  readonly size_mm: { readonly w: number; readonly h: number }
  readonly device?: { readonly type: string; readonly preset?: string; readonly config?: Readonly<Record<string, unknown>> }
  readonly nets?: readonly PartNet[]
  readonly connectors: readonly Connector[]
  readonly power?: { readonly elements?: readonly PowerElement[]; readonly limits?: { readonly max_mA?: number; readonly max_mV?: number } }
  readonly chips?: readonly ChipPlacement[]
}

/** A part with its drawing (the SVG text) and where it comes from. */
export interface LibraryPart extends Part {
  readonly svg: string
  readonly origin: 'library' | 'project'
}

/** The parts by id. */
export type Library = ReadonlyMap<string, LibraryPart>

export const CATEGORY_LABELS: Readonly<Record<PartCategory, string>> = {
  board: 'Boards',
  module: 'Modules and devkits',
  chip: 'Chips',
  actuator: 'Actuators',
  sensor: 'Sensors',
  passive: 'Passive parts',
  power: 'Power',
}

/** A part kept in a project file: part.json's content with its drawing. */
export interface StoredPart extends Part {
  readonly svg: string
}

const jsons = import.meta.glob<Part>('@library/parts/*/part.json', { eager: true, import: 'default' })
const drawings = import.meta.glob<string>('@library/parts/*/part.svg', { eager: true, query: '?raw', import: 'default' })

const folderOf = (path: string): string => path.split('/').slice(-2)[0] ?? path

let bundled: Library | undefined
/** The parts of _SOFTWARE/library, bundled with the app. */
export const bundledLibrary = (): Library => {
  if (bundled) return bundled
  const svgs = new Map(Object.entries(drawings).map(([path, svg]) => [folderOf(path), svg]))
  bundled = new Map(Object.entries(jsons).map(([path, json]) => {
    const { $schema: _schema, kind: _kind, graphic: _graphic, ...part } = json as Part & { $schema?: string; kind?: string; graphic?: string }
    return [part.id, { ...part, svg: svgs.get(folderOf(path)) ?? '', origin: 'library' as const }]
  }))
  return bundled
}

/** The library with the project's own parts over it. */
export const withProjectParts = (library: Library, parts: readonly StoredPart[] = []): Library =>
  parts.length === 0 ? library : new Map([...library, ...parts.map((p): [string, LibraryPart] => [p.id, { ...p, origin: 'project' }])])

/** The drawing of the board the project is on (board.json's name and revision); undefined: the library has none yet. */
export const boardPartFor = (library: Library, board: Pick<BoardDescriptor, 'name' | 'revision'>): LibraryPart | undefined =>
  [...library.values()].find((p) => p.category === 'board' && p.board?.name === board.name && p.board.revision === board.revision)

/** A drawing as an image source (the palette's thumbnails). */
export const svgUrl = (svg: string): string => `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`

/** The part's net by id. */
export const netOf = (part: Part, id: string | undefined): PartNet | undefined => (id === undefined ? undefined : part.nets?.find((n) => n.id === id))

/** Every pin of a part with its connector, keyed `connector.pin`. */
export const partPins = (part: Part): { key: string; connector: Connector; pin: PartPin }[] =>
  part.connectors.flatMap((connector) => connector.pins.map((pin) => ({ key: `${connector.id}.${pin.id}`, connector, pin })))

/** The input nets of an element. */
export const elementInputs = (element: PowerElement): readonly string[] =>
  element.in === undefined ? [] : typeof element.in === 'string' ? [element.in] : element.in
