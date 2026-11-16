import { Card } from '@ui/components'

/**
 * The firmware of the board. A new one is flashed over USB for now: the firmware has no update over the link (SYS_OTA.MD is not in
 * it yet), so this card only says so; it comes back with that module.
 */
export function FirmwareCard({ connected }: { connected: boolean }) {
  return <Card title="Firmware" subhead={connected ? 'Updating the firmware over this link is not in the firmware yet: flash it over USB.' : 'Connect the board.'} />
}
