import { BOARD_REF, endpointKey } from '../../domain/hardware'
import type { Endpoint, PinSpec } from '../../domain/hardware'
import { terminalAt } from './boardArt'

/*
 * Finding and measuring wire endpoints in the canvas DOM. A board terminal is a group of the board art (its art ID); a
 * device pin is the `pin-<name>` group of its symbol, whose `pin-<name>-t` circle is where a wire attaches. The groups
 * carry `data-port` for a device pin.
 */

/** The wirable pin under an event target: a board terminal, or a pin the device's variant brings out. */
export function endpointAt(target: EventTarget | null, boardRoot: Element | null, pinsOf: (device: string) => ReadonlyMap<string, PinSpec> | undefined, terminals: ReadonlyMap<string, PinSpec>): Endpoint | undefined {
  if (!(target instanceof Element)) return undefined
  const owner = target.closest<HTMLElement>('[data-device]')
  if (owner?.dataset.device) {
    const group = target.closest('g[id^="pin-"]')
    const pin = group && owner.contains(group) ? (group.getAttribute('data-port') ?? group.id.slice('pin-'.length)) : undefined
    return pin && pinsOf(owner.dataset.device)?.has(pin) ? { device: owner.dataset.device, pin } : undefined
  }
  const terminal = terminalAt(target, boardRoot)
  return terminal && terminals.has(terminal.id) ? { device: BOARD_REF, pin: terminal.id } : undefined
}

const quote = (value: string): string => value.replace(/["\\]/g, '\\$&')

/** The element a wire attaches to: the terminal's circle (its screw or hole), else its group. */
export function endpointElement(world: ParentNode, endpoint: Endpoint): Element | null {
  const group = endpoint.device === BOARD_REF
    ? world.querySelector(`.board-art-layer [id="${quote(endpoint.pin)}"]`)
    : world.querySelector(`[data-device="${quote(endpoint.device)}"] [id="pin-${quote(endpoint.pin)}-t"]`) ?? world.querySelector(`[data-device="${quote(endpoint.device)}"] g[data-port="${quote(endpoint.pin)}"]`)
  return group ? (group.tagName.toLowerCase() === 'circle' ? group : (group.querySelector('circle') ?? group)) : null
}

export type EndpointPositions = ReadonlyMap<string, { readonly x: number; readonly y: number }>

/** Where the endpoints are, in the world's own pixels (the canvas pixels at zoom 1), whatever the viewport and the devices' rotation. */
export function measureEndpoints(world: HTMLElement, endpoints: readonly Endpoint[], zoom: number): EndpointPositions {
  const origin = world.getBoundingClientRect()
  const found = new Map<string, { x: number; y: number }>()
  for (const endpoint of endpoints) {
    const element = endpointElement(world, endpoint)
    if (!element) continue
    const rect = element.getBoundingClientRect()
    found.set(endpointKey(endpoint), { x: (rect.left + rect.width / 2 - origin.left) / zoom, y: (rect.top + rect.height / 2 - origin.top) / zoom })
  }
  return found
}
