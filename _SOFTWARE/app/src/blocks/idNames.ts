import boardJson from '@data-structures/board/board.generated.json'
import contractsJson from '@data-structures/contracts/contracts.generated.json'

/*
 * The names behind the ids a block field holds (`id_kind` in its descriptor), read from the generated descriptors: a board device by
 * its alias (board.generated.json, from the `board_device` group in C), a device event by its alias (the contracts' events). An id the
 * descriptors do not know (a device the user installs) is shown as its number.
 */

const BOARD_DEVICES = new Map((boardJson as unknown as { devices: { id: number; alias: string }[] }).devices.map((device) => [device.id, device.alias]))
const EVENTS = new Map(
  (contractsJson as unknown as { device_contracts: { events?: { id: number; alias: string }[] }[] }).device_contracts.flatMap((contract) => (contract.events ?? []).map((event) => [event.id, event.alias] as const)),
)

/** What a field of `idKind` holding `value` names, or undefined when there is nothing better than the number. */
export const idName = (idKind: string | undefined, value: number): string | undefined => {
  if (idKind === 'device') return BOARD_DEVICES.get(value)
  if (idKind === 'event') return EVENTS.get(value)
  return undefined
}
