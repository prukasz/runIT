import type { CatalogEntry, FieldValues, PinRef, ServiceCatalog } from '@backend'
import { emptyProject } from '../project/projectFile'
import type { ProfileReservedPin, ProjectDevice, ProjectFile, Step } from '../project/projectFile'
import { createConfig } from './boardProfile'
import { boardType } from './devices'
import type { BoardDescriptor, BoardDevice, BoardKind } from './boardDescriptor'

/*
 * The board's profile as the app edits and keeps it, pure. The board is a list of devices, each its install (type and config, its
 * pins and place on an I2C bus inside the config) and its boot package (`defaults`: the commands it gets after it is created, at
 * every start of the board), the way a project's devices are: the profile is a project file whose `devices` are the board's, an I2C
 * bus or a UART port among them (a device of its own). What is not a device (the reserved pins, the power layout) stays the bundled board's.
 *
 *   profileFromBoard   the profile a board starts from: its bundled devices
 *   boardFromProfile   the board descriptor the app and the board's record are made of: the bundled board's static parts and the
 *                      profile's devices, each after the devices whose pins it uses (the board can only create in that order)
 *   bootSteps          the boot package of each device, by id
 *
 * The profile is kept by the app (`useBoardProfile`) and by the board (SYS_BOARD.MD, as the JSON of the project file).
 */

/** Where the app keeps the profile of a kind of board. */
export const profileKey = (kind: BoardKind): string => `runit.board.${kind}`

/** The config fields a device's `i2c` is kept in (`createConfig` writes them). */
const I2C_BUS = 'i2c_bus'
const I2C_ADDR = 'i2c_addr'

/** The profile a board starts from: its bundled devices, each as the config its CREATE carries. */
export const profileFromBoard = (board: BoardDescriptor, catalog: ServiceCatalog): ProjectFile => ({
  ...emptyProject(board.name),
  reservedPins: ownReserved(board),
  power: board.power,
  devices: board.devices.flatMap((d): ProjectDevice[] => {
    const type = boardType(catalog, d.descriptor)
    // The pins the bundled board sets up at start (its `pin_setup`) are the device's Board boot: its MODE, then its LEVEL.
    const setup = board.pinSetup.filter((p) => p.device === d.id).flatMap((p): Step[] => [
      { kind: 'device', device: d.id, contract: IO, command: MODE, args: { pin: p.pin, mode: p.mode } },
      ...(p.level !== undefined ? [{ kind: 'device' as const, device: d.id, contract: IO, command: LEVEL, args: { pin: p.pin, level: p.level ? 1 : 0 } }] : []),
    ])
    return type ? [{ id: d.id, type: type.id, name: d.name, config: createConfig(catalog, d) as FieldValues, defaults: setup }] : []
  }),
  // Their labels are the pins' names.
  pinAliases: board.pinSetup.map((p) => ({ device: p.device, pin: p.pin, name: p.label })),
})

const IO = 'IO'
const MODE = 'MODE'
const LEVEL = 'LEVEL'

/** The pins a profile's Board boot sets up (a MODE of the IO contract, and a LEVEL after it): what the app shows of them. */
const pinSetupOf = (profile: ProjectFile): BoardDescriptor['pinSetup'] =>
  profile.devices.flatMap((d) => (d.defaults ?? []).flatMap((s, i, all) => {
    if (s.kind !== 'device' || s.contract !== IO || s.command !== MODE || typeof s.args?.pin !== 'number') return []
    const pin = s.args.pin
    const level = all.slice(i + 1).find((x) => x.kind === 'device' && x.contract === IO && x.command === LEVEL && x.args?.pin === pin)
    const name = profile.pinAliases.find((a) => a.device === d.id && a.pin === pin)?.name
    return [{
      device: d.id, pin, mode: String(s.args.mode), label: name ?? '',
      ...(level?.kind === 'device' && level.args?.level !== undefined ? { level: Number(level.args.level) === 1 || level.args.level === true } : {}),
    }]
  }))

const isPin = (value: unknown): value is PinRef => typeof value === 'object' && value !== null && 'device' in value && 'pin' in value

/** The descriptor file name of a type (`device_tca6424a`): what a board device names. */
const descriptorOf = (type: CatalogEntry): string => /([^/]+)\.json$/.exec(type.source)?.[1] ?? type.id

/** A profile device as the board's descriptor has it: the pins and the I2C place out of the config. */
const boardDevice = (device: ProjectDevice, type: CatalogEntry, base: BoardDevice | undefined): BoardDevice => {
  const config: Record<string, unknown> = { ...device.config }
  const pins: (BoardDevice['pins'][number])[] = []
  for (const field of type.config?.fields ?? []) {
    if (!field.pinRef) continue
    const value = config[field.id]
    delete config[field.id]
    if (Array.isArray(value)) value.forEach((pin, slot) => { if (isPin(pin)) pins.push({ use: `${field.id}[${slot}]`, slot, device: pin.device, pin: pin.pin }) })
    else if (isPin(value)) pins.push({ use: field.id, device: value.device, pin: value.pin })
  }
  const bus = config[I2C_BUS]
  const address = config[I2C_ADDR]
  const i2c = typeof bus === 'number' && typeof address === 'number' ? { bus, address } : undefined
  if (i2c) { delete config[I2C_BUS]; delete config[I2C_ADDR] }
  return {
    id: device.id,
    name: device.name,
    ...(base?.description ? { description: base.description } : {}),
    descriptor: descriptorOf(type),
    pins,
    config,
    ...(i2c ? { i2c } : {}),
    ...(base?.ranges ? { ranges: base.ranges } : {}),
  }
}

/** The devices each after the devices whose pins it uses and the bus it is on; the others as listed. A cycle keeps the listed order between its devices. */
const inCreationOrder = (devices: readonly BoardDevice[]): BoardDevice[] => {
  const byId = new Map(devices.map((d) => [d.id, d]))
  const placed = new Set<number>()
  const out: BoardDevice[] = []
  const place = (d: BoardDevice, below: ReadonlySet<number>) => {
    if (placed.has(d.id) || below.has(d.id)) return
    const path = new Set(below).add(d.id)
    for (const pin of d.pins) {
      const used = byId.get(pin.device)
      if (used) place(used, path)
    }
    // A chip on an I2C bus is created after the bus device it names.
    const bus = d.i2c && byId.get(d.i2c.bus)
    if (bus) place(bus, path)
    if (!placed.has(d.id)) {
      placed.add(d.id)
      out.push(d)
    }
  }
  devices.forEach((d) => place(d, new Set()))
  return out
}

/** The board the profile makes: `base`'s static parts and the profile's devices (a device whose type no descriptor describes is left out). */
export const boardFromProfile = (profile: ProjectFile, base: BoardDescriptor, catalog: ServiceCatalog): BoardDescriptor => {
  const full = withBoard(profile, base)
  // Everything from the profile; of the bundled board only its name and revision (which drawing of the hardware library it is).
  return {
    name: base.name,
    revision: base.revision,
    pinSetup: pinSetupOf(profile),
    power: full.power ?? base.power,
    reservedPins: full.reservedPins ?? [],
    devices: inCreationOrder(profile.devices.flatMap((d) => {
    const type = catalog.devices.get(d.type)
    return type ? [boardDevice(d, type, base.devices.find((b) => b.id === d.id))] : []
  })),
  }
}

/** The bundled board's reserved pins. */
const ownReserved = (board: BoardDescriptor): ProfileReservedPin[] => board.reservedPins.map((r) => ({ ...r }))

/** A whole profile: one kept (or read from a board) without its reserved pins or power has the bundled board's. */
export const withBoard = (profile: ProjectFile, base: BoardDescriptor): ProjectFile => ({
  ...profile,
  reservedPins: profile.reservedPins ?? ownReserved(base),
  power: profile.power ?? base.power,
})

/** The boot package of every device of the profile that has one. */
export const bootSteps = (profile: ProjectFile): ReadonlyMap<number, readonly Step[]> =>
  new Map(profile.devices.flatMap((d) => (d.defaults && d.defaults.length > 0 ? [[d.id, d.defaults] as const] : [])))
