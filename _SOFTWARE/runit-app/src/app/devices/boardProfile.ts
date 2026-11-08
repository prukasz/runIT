import { createDeviceCall, crc32, deviceCall, frameImage, recordInfo, requestFrame, serviceCall, storeRecord, CommandError } from '@backend'
import type { BoardDescriptor, LinkDefinitions, RunitLink, ServiceCatalog } from '@backend'
import { parseProject, serializeProject } from '../project/projectFile'
import type { ProjectFile, Step } from '../project/projectFile'
import { boardType, SINGLE_TYPES, stepFrame } from './devices'

/*
 * The board's profile (SYS_BOARD.MD): what the board is, kept on it. The firmware runs the record at every start and checks
 * nothing; this module writes it and compares it with the board's descriptor.
 *
 *   record  frames, in the order a device can only use what exists: the power layout and its rails, the I2C buses and the UART
 *           ports (on the chip's GPIO pins), then each device's CREATE with the flag of the board's own, each followed by the set-up of its pins. The chip's
 *           own GPIO (a single type) has no CREATE: the firmware makes it at every boot, the frames after it only name its pins
 *           and then the device's start commands (its boot package, `ProjectDevice.defaults` of the profile)
 *   JSON    the profile as the app keeps it (`boardEditor.ts`: a project file whose devices are the board's), for reading back;
 *           the firmware never reads it
 */

/** The profile's record: class 0x04, index 0 (`SYS_BOARD_RECORD_ID`). */
export const BOARD_RECORD_ID = 0x04000000
/** A chunk of the JSON: fits one frame both ways. */
const JSON_CHUNK = 400
const BOARD_SERVICE = 'BOARD'

const frameOf = (call: { readonly request: Parameters<typeof requestFrame>[0] }): Uint8Array => requestFrame(call.request)

/** A device's create struct from board.json: its config, its place on an I2C bus, its pins (those with a slot in the list of pins). */
export const createConfig = (catalog: ServiceCatalog, device: BoardDescriptor['devices'][number]): Record<string, unknown> => {
  const config: Record<string, unknown> = { ...device.config }
  if (device.i2c) Object.assign(config, { i2c_bus: device.i2c.bus, i2c_addr: device.i2c.address })
  const fields = boardType(catalog, device.descriptor)?.config?.fields ?? []
  for (const pin of device.pins) {
    const at = { device: pin.device, pin: pin.pin }
    if (pin.slot === undefined) {
      config[pin.use] = at
      continue
    }
    const list = fields.find((f) => f.pinRef && (f.size ?? 0) > 3)
    if (!list) throw new Error(`${device.name}: the pin ${pin.use} has a slot, its type no list of pins.`)
    const pins = (config[list.id] ??= Array.from({ length: (list.size ?? 0) / 3 }, () => null)) as unknown[]
    pins[pin.slot] = at
  }
  return config
}

/** The frames of the profile's record. */
export const boardProfileFrames = (defs: LinkDefinitions, catalog: ServiceCatalog, board: BoardDescriptor, boot: ReadonlyMap<number, readonly Step[]> = new Map()): Uint8Array[] => {
  const none = catalog.allDevices!
  const { limits, input, usbPd, sourcePins, rails } = board.power
  const frames: Uint8Array[] = [
    frameOf(serviceCall(catalog, 'POWER_MANAGER', 'LAYOUT', {
      ...limits,
      input_device: input?.device ?? none,
      input_channel: input?.channel ?? 0,
      usb_pd: usbPd ?? none,
      usb_ok: sourcePins?.usbOk ?? null,
      ext_ok: sourcePins?.extOk ?? null,
    } as never)),
    ...rails.map((r) => frameOf(serviceCall(catalog, 'POWER_MANAGER', 'RAIL', {
      vreg: r.regulator,
      monitor_device: r.monitor?.device ?? none,
      monitor_channel: r.monitor?.channel ?? 0,
      min_mA: r.min_mA,
    }))),
    // The I2C buses before any device: their pins are the chip's GPIO, which the firmware makes before the record runs, and the
    // devices on a bus need it to exist.
    ...board.i2cBuses.map((bus) => frameOf(serviceCall(catalog, 'I2C', 'BUS_CREATE', { bus: bus.bus, sda: bus.sda, scl: bus.scl, pullup: bus.pullup ? 1 : 0 } as never, 'write'))),
    // The UART ports too: their pins are the chip's GPIO as well, and a device may talk over one.
    ...(board.uartPorts ?? []).map((port) => frameOf(serviceCall(catalog, 'UART', 'CREATE', port as never, 'write'))),
  ]
  for (const device of board.devices) {
    const type = boardType(catalog, device.descriptor)
    if (!type) throw new Error(`${device.name}: no descriptor ${device.descriptor}.`)
    if (!SINGLE_TYPES.has(type.id)) frames.push(frameOf(createDeviceCall(defs, catalog, device.id, type.id, createConfig(catalog, device) as never, true)))
    // A device with a Board boot sets its pins up there (the profile's: boardEditor.ts); the bundled board's own setup otherwise.
    for (const pin of boot.has(device.id) ? [] : board.pinSetup.filter((p) => p.device === device.id)) {
      frames.push(frameOf(deviceCall(defs, catalog, device.id, 'IO', 'MODE', { pin: pin.pin, mode: pin.mode }, 'write')))
      if (pin.level !== undefined) frames.push(frameOf(deviceCall(defs, catalog, device.id, 'IO', 'LEVEL', { pin: pin.pin, level: pin.level ? 1 : 0 }, 'write')))
    }
    for (const step of boot.get(device.id) ?? []) frames.push(stepFrame(defs, catalog, step))
  }
  return frames
}

/** The JSON the board keeps: the profile as the app keeps it, so the same profile gives the same bytes. */
export const boardProfileJson = (profile: ProjectFile): Uint8Array => new TextEncoder().encode(serializeProject(profile))

interface JsonInfo { readonly bytes: number; readonly crc32: number }

const storedRecord = async (link: RunitLink): Promise<{ readonly bytes: number; readonly crc: number } | undefined> => {
  try {
    return await recordInfo(link, BOARD_RECORD_ID)
  } catch (failure) {
    if (failure instanceof CommandError && failure.code === 'device-error') return undefined
    throw failure
  }
}

const jsonInfo = async (link: RunitLink, catalog: ServiceCatalog): Promise<JsonInfo> => {
  const answer = await link.request(serviceCall(catalog, BOARD_SERVICE, 'JSON_INFO').request, { label: 'BOARD JSON_INFO' })
  const view = new DataView(answer.buffer, answer.byteOffset, answer.byteLength)
  return { bytes: view.getUint32(0, true), crc32: view.getUint32(4, true) }
}

/** Whether the board holds this profile: the record and the JSON, each by its length and CRC. */
export const boardHasProfile = async (link: RunitLink, catalog: ServiceCatalog, board: BoardDescriptor, profile: ProjectFile, boot: ReadonlyMap<number, readonly Step[]>): Promise<boolean> => {
  const image = frameImage(boardProfileFrames(link.defs, catalog, board, boot))
  const json = boardProfileJson(profile)
  const record = await storedRecord(link)
  if (!record || record.bytes !== image.length || record.crc !== crc32(image)) return false
  const info = await jsonInfo(link, catalog)
  return info.bytes === json.length && info.crc32 === crc32(json)
}

/** Writes the profile: the JSON, checked, then the record. The caller restarts the board: only a boot runs it. */
export const writeBoardProfile = async (link: RunitLink, catalog: ServiceCatalog, board: BoardDescriptor, profile: ProjectFile, boot: ReadonlyMap<number, readonly Step[]>): Promise<void> => {
  const json = boardProfileJson(profile)
  await link.request(serviceCall(catalog, BOARD_SERVICE, 'JSON_ERASE').request, { label: 'BOARD JSON_ERASE' })
  for (let offset = 0; offset < json.length; offset += JSON_CHUNK) {
    const data = json.subarray(offset, offset + JSON_CHUNK)
    await link.request(serviceCall(catalog, BOARD_SERVICE, 'JSON_APPEND', { offset, data }).request, { label: 'BOARD JSON_APPEND' })
  }
  const info = await jsonInfo(link, catalog)
  if (info.bytes !== json.length || info.crc32 !== crc32(json)) throw new Error('The board\'s profile did not read back whole: connect again to write it again.')
  await storeRecord(link, BOARD_RECORD_ID, boardProfileFrames(link.defs, catalog, board, boot))
}

/** The profile the board keeps as JSON; none when it keeps none (or not one this app reads). */
export const readBoardProfile = async (link: RunitLink, catalog: ServiceCatalog): Promise<ProjectFile | undefined> => {
  const info = await jsonInfo(link, catalog)
  if (info.bytes === 0) return undefined
  const bytes = new Uint8Array(info.bytes)
  for (let chunk = 0, at = 0; at < info.bytes; chunk++) {
    const data = await link.request(serviceCall(catalog, BOARD_SERVICE, 'JSON_READ', { chunk }).request, { label: 'BOARD JSON_READ' })
    if (data.length === 0) break
    bytes.set(data.subarray(0, info.bytes - at), at)
    at += data.length
  }
  const read = parseProject(new TextDecoder().decode(bytes))
  return read.ok ? read.project : undefined
}
