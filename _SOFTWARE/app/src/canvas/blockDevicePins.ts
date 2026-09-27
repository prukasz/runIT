import type { DeviceCatalog, DeviceChoice, VmBlockField } from '../domain/descriptors'
import { allDevices, pinKey, pinsOf, pinUsers } from '../domain/devices'
import type { ProjectDevice } from '../domain/project'

export const blockDevices = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], field: VmBlockField) =>
  allDevices(catalog, devices).filter((device) => (!device.system || catalog.board.find((board) => board.deviceId === device.deviceId)?.installed) && (!field.contract || device.type?.contracts.some((contract) => contract.id === field.contract)))

/** Pins reserved by the board or locked by a device cannot be VM destinations. */
export const blockDevicePins = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], deviceId: number, contract?: string): readonly DeviceChoice[] => {
  const device = allDevices(catalog, devices).find((entry) => entry.deviceId === deviceId)
  const users = pinUsers(catalog, devices)
  const choices = device?.type?.contracts.find((entry) => entry.id === contract)?.parameters.find((parameter) => parameter.name === 'pin')?.choices ?? pinsOf(device?.type)
  return choices.filter((pin) => !catalog.reservedPins.some((reserved) => reserved.deviceId === deviceId && reserved.pin === pin.value) && !users.get(pinKey(deviceId, pin.value))?.some((user) => user.owner))
}
