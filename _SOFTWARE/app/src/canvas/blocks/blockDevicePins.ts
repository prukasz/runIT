import type { DeviceCatalog, DeviceChoice, VmBlockField } from '../../domain/descriptors'
import { allDevices, pinKey, pinsOf, pinUsers } from '../../domain/devices'
import type { ProjectDevice } from '../../domain/project'

export const blockDevices = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], field: VmBlockField) =>
  allDevices(catalog, devices).filter((device) => (!device.system || catalog.board.find((board) => board.deviceId === device.deviceId)?.installed) && (!field.contract || device.type?.id === field.contract || device.type?.contracts.some((contract) => contract.id === field.contract)))

/** The parameter of the field's contract that an `@id pin` setting picks: a `pin`, or the `channel` of a measuring device (a power monitor's channel is not an IO pin). */
const indexParameter = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], deviceId: number, contract?: string) =>
  allDevices(catalog, devices).find((entry) => entry.deviceId === deviceId)?.type?.contracts.find((entry) => entry.id === contract)?.parameters.find((parameter) => parameter.name === 'pin' || parameter.name === 'channel')

/** What the setting indexes: an IO pin of the device, or one of its channels. */
export const blockIndexKind = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], deviceId: number | undefined, contract?: string): 'pin' | 'channel' =>
  deviceId !== undefined && indexParameter(catalog, devices, deviceId, contract)?.name === 'channel' ? 'channel' : 'pin'

/** Pins reserved by the board or locked by a device cannot be VM destinations. Channels are the device's own list. */
export const blockDevicePins = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], deviceId: number, contract?: string): readonly DeviceChoice[] => {
  const device = allDevices(catalog, devices).find((entry) => entry.deviceId === deviceId)
  const users = pinUsers(catalog, devices)
  const parameter = indexParameter(catalog, devices, deviceId, contract)
  if (parameter?.name === 'channel') return parameter.choices ?? []
  const choices = parameter?.choices ?? pinsOf(device?.type)
  return choices.filter((pin) => !catalog.reservedPins.some((reserved) => reserved.deviceId === deviceId && reserved.pin === pin.value) && !users.get(pinKey(deviceId, pin.value))?.some((user) => user.owner))
}
