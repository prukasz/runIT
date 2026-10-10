import type { DevicePort, DeviceType } from '../../domain/descriptors'
import { BOARD_REF } from '../../domain/hardware'
import type { PinSpec, WiringContext, WiringDevice } from '../../domain/hardware'
import type { BoardLayout, PinLimit, ProjectDevice } from '../../domain/project'
import type { BoardModel } from './boardModel'
import { pictureVariants } from './pictureSymbols'
import { variantOf } from './symbols'

/*
 * What a device's pins are on the canvas: the pins its variant brings out, each with the electrical meaning the pins
 * csv gives it (a picture variant can override limits, direction and modes per pin). These are the endpoints a wire
 * can have; a pin the variant does not bring out cannot be wired.
 */

/** The bus signal of a bus pin: its csv group and name, e.g. `i2c-sda`. */
const signalOf = (port: DevicePort): string | undefined => (port.kind === 'bus' && port.group ? `${port.group}-${port.name.toLowerCase()}` : undefined)

export function portSpec(port: DevicePort, override?: { dir?: DevicePort['dir']; modes?: readonly string[]; mV_min?: number; mV_max?: number }): PinSpec {
  const mV_min = override?.mV_min ?? port.mV_min
  const mV_max = override?.mV_max ?? port.mV_max
  const signal = signalOf(port)
  return {
    kind: port.kind,
    dir: override?.dir ?? port.dir,
    label: port.name,
    modes: override?.modes ?? port.modes ?? [],
    ...(mV_min !== undefined ? { mV_min } : {}),
    ...(mV_max !== undefined ? { mV_max } : {}),
    ...(signal ? { signal } : {}),
    ...(port.cfg_field ? { cfgField: port.cfg_field } : {}),
    ...(port.bind_pin !== undefined ? { bindPin: port.bind_pin } : {}),
    ...(port.note ? { note: port.note } : {}),
  }
}

/** The wirable pins of a device type as its variant draws it, by pin name. */
export function devicePinSpecs(type: DeviceType | undefined, variantId: string | undefined, pinLimits?: Readonly<Record<string, PinLimit>>): ReadonlyMap<string, PinSpec> {
  const pins = new Map<string, PinSpec>()
  if (!type) return pins
  const variant = variantOf(type, variantId)
  const picture = variant?.picture ? pictureVariants(type).find((entry) => entry.id === variant.id) : undefined
  if (picture) {
    for (const anchor of picture.layout.anchors) pins.set(anchor.port.name, portSpec(anchor.port, { dir: anchor.dir, modes: anchor.modes, mV_min: pinLimits?.[anchor.port.name]?.mV_min ?? anchor.mV_min, mV_max: pinLimits?.[anchor.port.name]?.mV_max ?? anchor.mV_max }))
    return pins
  }
  const hidden = new Set(variant?.hidden ?? [])
  for (const port of type.ports) if (!hidden.has(port.name)) pins.set(port.name, portSpec(port, pinLimits?.[port.name]))
  return pins
}

/** The devices on the canvas as the wiring checks see them. */
export const wiringDevices = (devices: readonly ProjectDevice[], typeOf: (id: string) => DeviceType | undefined, layout: BoardLayout | undefined, nameOf: (device: ProjectDevice) => string): WiringDevice[] =>
  devices.map((device) => ({ ref: device.id, name: nameOf(device), pins: devicePinSpecs(typeOf(device.type), layout?.[device.id]?.variant, layout?.[device.id]?.pinLimits) }))

export const wiringContext = (devices: readonly WiringDevice[], board: Pick<BoardModel, 'nets' | 'terminals'>): WiringContext => ({ nets: board.nets, board: board.terminals, devices })

export { BOARD_REF }
