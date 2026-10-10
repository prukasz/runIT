import type { DeviceCatalog, DeviceChoice, DeviceEventSource } from '../descriptors'
import type { ProjectDevice } from '../project'
import { allDevices } from './devices'

/*
 * Who can raise which events: the event catalog of a block's source pickers (a device ID, a channel, an event, narrowed by the
 * event domain). A device lists what it publishes in its descriptor (`events`); a source that names no device can raise every event
 * of the domain. A choice of "any" (the field's wildcard) is the picker's, not listed here.
 */

const sourcesOf = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], domainEnum: string, deviceId: number | undefined): readonly DeviceEventSource[] =>
  deviceId === undefined ? [] : allDevices(catalog, devices).find((device) => device.deviceId === deviceId)?.type?.eventSources.filter((source) => source.domainEnum === domainEnum) ?? []

/** The installed devices that publish in `domain` (any domain when it is undefined). */
export const eventSourceDevices = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], domainEnum: string, domain: number | undefined) =>
  allDevices(catalog, devices).filter((device) =>
    (!device.system || catalog.board.find((board) => board.deviceId === device.deviceId)?.installed) &&
    device.type?.eventSources.some((source) => source.domainEnum === domainEnum && (domain === undefined || source.domain === domain)))

/** The channels the device's events carry in `domain`: nothing for any domain or device (those match every channel), the labelled channels of its source otherwise. */
export const eventSourceChannels = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], domainEnum: string, domain: number | undefined, deviceId: number | undefined): { readonly label?: string; readonly choices: readonly DeviceChoice[] } => {
  const source = domain === undefined ? undefined : sourcesOf(catalog, devices, domainEnum, deviceId).find((entry) => entry.domain === domain)
  return { ...(source?.channelLabel ? { label: source.channelLabel } : {}), choices: source?.channels ?? [] }
}

/** The events the device raises in `domain`; with no device, every event of the domain; nothing for any domain. */
export const eventSourceEvents = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], domainEnum: string, domain: number | undefined, deviceId: number | undefined): readonly DeviceChoice[] => {
  if (domain === undefined) return []
  if (deviceId === undefined) return catalog.domainEvents(domainEnum, domain)
  return sourcesOf(catalog, devices, domainEnum, deviceId).find((entry) => entry.domain === domain)?.events ?? []
}
