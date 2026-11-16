import boardJson from '@generated/common/board.json'
import devkitBoardJson from '@generated/common/board_devkit.json'
import { parseRanges } from '@backend'
import type { FieldRanges } from '@backend'

/*
 * The board the app describes: what the app writes to the board's profile (SYS_BOARD.MD: the record of frames the board runs at
 * boot, and this descriptor as the JSON the board keeps for the app). The descriptors are the app's own (`boards/board.json`,
 * `board_devkit.json`, checked by `board.schema.json`); the firmware never reads them.
 */

/** A pin of a device as board.json names it. */
export interface BoardPin {
  readonly device: number
  readonly pin: number
}

/** A device the firmware creates at boot (board.json `devices`): the board's own, which a project's change leaves (SYS_DEVICE.MD). */
export interface BoardDevice {
  readonly id: number
  readonly name: string
  readonly description?: string
  /** The device descriptor's file name (`device_tca6424a`). */
  readonly descriptor: string
  /** The pins of other devices it uses, by what for (`intr_pin`, `in_pin[0]`); `slot`: its place in a config field that is a list of pins. */
  readonly pins: readonly (BoardPin & { readonly use: string; readonly slot?: number; readonly description?: string })[]
  /** The config values board.json gives (the others are the descriptor's defaults). */
  readonly config: Readonly<Record<string, unknown>>
  /** Where it sits on an I2C bus: the id of the bus device (a device of the board, `device_esp32s3_i2c`) and its address. */
  readonly i2c?: { readonly bus: number; readonly address: number }
  /** The ranges board.json gives for the fields of its commands (`COMMAND.field`); they replace the device type's. */
  readonly ranges?: FieldRanges
}

/** What board.json says of the board: its devices (the I2C buses and UART ports among them), the pins set up at boot and the pins nothing else may take. */
export interface BoardDescriptor {
  readonly name: string
  /** The board's revision (board.json): with the name, which drawing of the hardware library is this board. */
  readonly revision: number
  readonly devices: readonly BoardDevice[]
  /** The pins the board's profile sets up: their `sys_io_mode_e` member name, an output's level and what they are for. */
  readonly pinSetup: readonly (BoardPin & { readonly label: string; readonly mode: string; readonly level?: boolean; readonly description?: string })[]
  readonly reservedPins: readonly (BoardPin & { readonly label: string; readonly reason?: string })[]
  /** Where the board's power is measured (`power` of board.json): the whole input and each regulator rail, by a power monitor's channel. */
  readonly power: BoardPower
}

/** A power monitor's channel (`POWER_MONITOR` commands take the channel). */
export interface PowerProbe {
  readonly device: number
  readonly channel: number
}

export interface BoardPower {
  /** The limits of the board's input (`power.limits` of board.json), which its profile gives the power manager. */
  readonly limits: { readonly max_mV: number; readonly max_mA: number; readonly reserve_mW: number; readonly unknown_source_mA: number; readonly vreg_efficiency_pct: number }
  readonly input?: PowerProbe & { readonly description?: string }
  /** The USB-PD sink, when the board has one. */
  readonly usbPd?: number
  /** The eFuse power-good pins of the two inputs, when the board has them. */
  readonly sourcePins?: { readonly usbOk: BoardPin; readonly extOk: BoardPin }
  readonly rails: readonly { readonly id: string; readonly name: string; readonly regulator: number; readonly monitor?: PowerProbe; readonly min_mA: number; readonly description?: string }[]
}

/**
 * The boards the app knows: the runIT PCB, or a plain ESP32-S3 devkit with only the ESP pins (no chips, no rails). The firmware
 * is the same for both; the app writes the board's profile from its descriptor (`app/devices/boardProfile.ts`).
 */
export type BoardKind = 'pcb' | 'devkit'

/** A bundled board (generated/common/board.json, board_devkit.json); by default the runIT PCB; the devkit's is what a profile file opens. */
export const bundledBoard = (kind: BoardKind = 'pcb'): BoardDescriptor => {
  const json = (kind === 'devkit' ? devkitBoardJson : boardJson) as unknown as {
    name: string
    revision: number
    devices: { id: number; name: string; description?: string; descriptor: string; ranges?: unknown; pins?: { use: string; slot?: number; device: number; pin: number; description?: string }[]; config?: Record<string, unknown>; i2c?: { bus: number; address: number } }[]
    pin_setup?: { device: number; pin: number; mode: string; level?: boolean; label: string; description?: string }[]
    reserved_pins?: { device: number; pin: number; label: string; reason?: string }[]
    power: {
      limits: BoardPower['limits']
      sources?: { usb_pd?: { device?: number; ok_pin?: BoardPin }; external?: { ok_pin?: BoardPin } }
      input_monitor?: { device: number; channel: number; description?: string }
      rails?: { id: string; name: string; vreg_device: number; monitor?: { device: number; channel: number }; min_mA: number; description?: string }[]
    }
  }
  const usbOk = json.power.sources?.usb_pd?.ok_pin
  const extOk = json.power.sources?.external?.ok_pin
  const usbPd = json.power.sources?.usb_pd?.device
  return {
    name: json.name,
    revision: json.revision,
    devices: json.devices.map((d) => ({ id: d.id, name: d.name, ...(d.description ? { description: d.description } : {}), descriptor: d.descriptor, pins: d.pins ?? [], config: d.config ?? {}, ...(d.i2c ? { i2c: { bus: d.i2c.bus, address: d.i2c.address } } : {}), ...(Object.keys(parseRanges(d.ranges)).length > 0 ? { ranges: parseRanges(d.ranges) } : {}) })),
    pinSetup: json.pin_setup ?? [],
    reservedPins: json.reserved_pins ?? [],
    power: {
      limits: json.power.limits,
      ...(json.power.input_monitor ? { input: json.power.input_monitor } : {}),
      ...(usbPd !== undefined ? { usbPd } : {}),
      ...(usbOk && extOk ? { sourcePins: { usbOk, extOk } } : {}),
      rails: (json.power.rails ?? []).map((r) => ({ id: r.id, name: r.name, regulator: r.vreg_device, ...(r.monitor ? { monitor: r.monitor } : {}), min_mA: r.min_mA, ...(r.description ? { description: r.description } : {}) })),
    },
  }
}
