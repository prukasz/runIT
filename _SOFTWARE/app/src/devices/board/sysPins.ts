import type { SysPinRef } from '../../domain/board'
import type { DeviceCatalog, DeviceType } from '../../domain/descriptors'
import { modesOf, pinDisplayLabel, pinKey, pinsOf, pinUsers } from '../../domain/devices'
import type { PinDir, PinSpec } from '../../domain/hardware'
import { boardDeviceRef } from '../../domain/project'
import type { ProjectDevice } from '../../domain/project'
import { BOARD_SYS_LIMITS } from './boardTerminals'

/*
 * The installed sys_io pins a board connector can bind to: every pin of a board device that provides pins (the ESP's
 * GPIO, the TCA6424A expander, the ADS7128 analog inputs, the PCA9685 PWM channels, the DAC), with what each one is
 * electrically and who already uses it.
 */

interface SysRole {
  readonly dir: PinDir
  readonly modes: readonly string[]
  readonly mV_min?: number
  readonly mV_max?: number
  /** Where the role came from, for the pin's tooltip. */
  readonly source: 'pins csv' | 'contracts'
}

/** A provider writes a pin (sets a level, a duty or a voltage, toggles it) or reads it (a level, a voltage). */
const WRITES = /^packet_sys_io_(set_(level|pwm_duty|voltage)|toggle)_t$/
const READS = /^packet_sys_io_get_(level|voltage)_t$/

const MODE_WORDS: ReadonlyArray<readonly [RegExp, string]> = [[/PWM/, 'PWM'], [/ADC/, 'ADC'], [/DAC/, 'DAC'], [/(INPUT|OUTPUT)/, 'BINARY']]

/**
 * What the pins of a provider do on a wire, from the catalog and not from a table of devices: a pin the device's pins csv
 * names (`bind_pin`) is what the csv says (direction, modes, limits); otherwise a provider that can write its pins
 * produces, one that can only read them consumes, one that does both is either. The modes are the ones the provider's
 * pin-mode contract offers, or what its contracts do (a duty is PWM, a voltage read is ADC, a voltage write is DAC).
 */
function roleOf(type: DeviceType, pin: number): SysRole {
  const port = type.ports.find((entry) => entry.bind_pin === pin)
  if (port) return { dir: port.dir, modes: port.modes ?? [], source: 'pins csv', ...(port.mV_min !== undefined ? { mV_min: port.mV_min } : {}), ...(port.mV_max !== undefined ? { mV_max: port.mV_max } : {}) }
  const ids = type.contracts.map((contract) => contract.id)
  const writes = ids.some((id) => WRITES.test(id))
  const reads = ids.some((id) => READS.test(id))
  const fromModes = modesOf(type).flatMap((mode) => MODE_WORDS.filter(([pattern]) => pattern.test(mode.symbol ?? mode.label)).map(([, word]) => word))
  const fromContracts = [
    ...(ids.includes('packet_sys_io_set_pwm_duty_t') ? ['PWM'] : []),
    ...(ids.includes('packet_sys_io_get_voltage_t') && !ids.includes('packet_sys_io_set_voltage_t') ? ['ADC'] : []),
    ...(ids.includes('packet_sys_io_set_voltage_t') ? ['DAC'] : []),
    ...(ids.some((id) => /_(set|get)_level_t$|toggle/.test(id)) ? ['BINARY'] : []),
  ]
  return { dir: writes && reads ? 'inout' : writes ? 'out' : reads ? 'in' : 'inout', modes: [...new Set(fromModes.length ? fromModes : fromContracts)], source: 'contracts' }
}

/** The electrical meaning of one sys pin, or undefined for a pin the board does not have. */
export function sysPinSpec(catalog: DeviceCatalog, ref: SysPinRef): PinSpec | undefined {
  const device = catalog.board.find((entry) => entry.deviceId === ref.device)
  const choice = device?.type && pinsOf(device.type).find((entry) => entry.value === ref.pin)
  if (!device?.type || !choice) return undefined
  const role = roleOf(device.type, ref.pin)
  const limit = BOARD_SYS_LIMITS[device.type.id]
  const port = device.type.ports.find((entry) => entry.bind_pin === ref.pin)
  const mV_min = role.mV_min
  const mV_max = role.mV_max ?? limit?.mV_max
  return {
    kind: 'io',
    dir: role.dir,
    label: `${device.name} ${pinDisplayLabel(catalog, boardDeviceRef(device.deviceId), ref.pin, port?.name ?? choice.label)}`,
    modes: role.modes,
    ...(mV_min !== undefined ? { mV_min } : {}),
    ...(mV_max !== undefined ? { mV_max } : {}),
    sysPin: ref,
  }
}

export interface SysPinOption {
  readonly pin: number
  readonly label: string
  /** Who already uses the pin (a board device or the board's own setup), when someone does. */
  readonly takenBy?: string
}

export interface SysPinProvider {
  readonly device: number
  readonly name: string
  readonly title: string
  readonly installed: boolean
  readonly pins: readonly SysPinOption[]
}

/** Every board device that provides pins, with its pins and who uses them. */
export function sysPinProviders(catalog: DeviceCatalog, devices: readonly ProjectDevice[]): SysPinProvider[] {
  const users = pinUsers(catalog, devices)
  return catalog.board
    .map((device) => ({ device, pins: pinsOf(device.type) }))
    .filter(({ pins }) => pins.length > 0)
    .map(({ device, pins }) => ({
      device: device.deviceId,
      name: device.name,
      title: device.title,
      installed: device.installed,
      pins: pins.map((pin) => {
        const taken = users.get(pinKey(device.deviceId, pin.value))?.map((user) => `${user.ownerName}: ${user.use}`).join(', ')
        // The pin as the rest of the app names it: its alias with the number beside it, else `Pin n`.
        return { pin: pin.value, label: pinDisplayLabel(catalog, boardDeviceRef(device.deviceId), pin.value, pin.label), ...(taken ? { takenBy: taken } : {}) }
      }),
    }))
}
