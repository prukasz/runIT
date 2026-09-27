import type { DeviceCatalog } from '../descriptors'
import type { ActionStep, DeviceRef, ProjectAction, ProjectDevice } from '../project'
import type { UploadDiagnostic } from '../upload'
import { findContract, pinUsers, resolveDevice } from './devices'
import type { ResolvedDevice } from './devices'

/*
 * PWM frequencies and the pins they touch. Two kinds of devices:
 * - one frequency for the whole device (PCA9685: `set_pwm_frequency`'s pin is
 *   `device_wide`): setting it changes every channel in use;
 * - a frequency per pin on a few shared timers (ESP GPIO: pins with the same
 *   frequency share one LEDC timer, `pwmFrequencies` of them): other pins
 *   never change, but a frequency past the timers is refused.
 * Only warnings: the user may mean it.
 */

export const SET_PWM_FREQUENCY = 'packet_sys_io_set_pwm_frequency_t'

/** A frequency the user sets: live from the command panel, or a step of the project. */
export interface FrequencySetting {
  readonly device: DeviceRef
  readonly pin?: number
  readonly hz: number
}

interface FrequencyUse extends FrequencySetting {
  /** Where it is set: `default settings`, `action 'lights'`. */
  readonly where: string
  /** Diagnostic subject: `setup:<device ref>` or the action's ID. */
  readonly subjectId: string
  readonly step: ActionStep
}

const numberValue = (value: ActionStep['values'][string] | undefined): number | undefined => (typeof value === 'number' ? value : undefined)

const frequencyUses = (setup: readonly ActionStep[], actions: readonly ProjectAction[]): FrequencyUse[] => {
  const use = (step: ActionStep, where: string, subjectId: string): FrequencyUse[] => {
    const hz = numberValue(step.values.frequency_Hz)
    if (step.contract !== SET_PWM_FREQUENCY || hz === undefined) return []
    const pin = numberValue(step.values.pin)
    return [{ device: step.device, ...(pin === undefined ? {} : { pin }), hz, where, subjectId, step }]
  }
  return [
    ...setup.flatMap((step) => use(step, 'default settings', `setup:${step.device}`)),
    ...actions.flatMap((action) => action.steps.flatMap((step) => use(step, `action '${action.name}'`, action.id))),
  ]
}

const isDeviceWide = (catalog: DeviceCatalog, device: ResolvedDevice): boolean =>
  findContract(catalog, device, SET_PWM_FREQUENCY)?.parameters.some((parameter) => parameter.deviceWide) ?? false

/** `8, 9, 10` → `8–10`. */
const pinList = (pins: readonly number[]): string => {
  const sorted = [...new Set(pins)].sort((a, b) => a - b)
  const runs: string[] = []
  for (let index = 0; index < sorted.length; index++) {
    const start = sorted[index]!
    while (index + 1 < sorted.length && sorted[index + 1] === sorted[index]! + 1) index++
    runs.push(start === sorted[index] ? `${start}` : `${start}–${sorted[index]}`)
  }
  return runs.join(', ')
}

/** Who uses which pins of a device, other than the frequency steps themselves: owner → pins. */
const pinsInUse = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], setup: readonly ActionStep[], actions: readonly ProjectAction[], device: ResolvedDevice): Map<string, number[]> => {
  const owners = new Map<string, number[]>()
  const add = (owner: string, pin: number) => owners.set(owner, [...(owners.get(owner) ?? []), pin])
  for (const [key, users] of pinUsers(catalog, devices)) {
    const [deviceId, pin] = key.split(':').map(Number)
    if (deviceId !== device.deviceId) continue
    for (const user of users) add(user.ownerName, pin!)
  }
  const steps = [...setup.map((step) => ({ step, where: 'default settings' })), ...actions.flatMap((action) => action.steps.map((step) => ({ step, where: `action '${action.name}'` })))]
  for (const { step, where } of steps) {
    const pin = numberValue(step.values.pin)
    if (step.device === device.ref && step.contract !== SET_PWM_FREQUENCY && pin !== undefined) add(where, pin)
  }
  return owners
}

/**
 * Warnings for setting a PWM frequency: on a one-frequency device the other
 * pins in use and other frequencies the project sets; on a per-pin device the
 * frequencies past its timers. `others` are the project's frequency steps
 * other than this one.
 */
const warningsFor = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], setup: readonly ActionStep[], actions: readonly ProjectAction[], setting: FrequencySetting, others: readonly FrequencyUse[]): string[] => {
  const device = resolveDevice(catalog, devices, setting.device)
  if (!device) return []
  const warnings: string[] = []
  const sameDevice = others.filter((use) => use.device === setting.device)
  if (isDeviceWide(catalog, device)) {
    const used = [...pinsInUse(catalog, devices, setup, actions, device)].map(([owner, pins]) => `${owner} (pins ${pinList(pins)})`)
    if (used.length) warnings.push(`${device.name} has one PWM frequency for all its pins: ${setting.hz} Hz also changes ${used.join(', ')}.`)
    const different = sameDevice.filter((use) => use.hz !== setting.hz)
    if (different.length) warnings.push(`${device.name} is also set to ${[...new Set(different.map((use) => `${use.hz} Hz (${use.where})`))].join(', ')}: the last one set wins, for every pin.`)
    return warnings
  }
  const limit = device.type?.pwmFrequencies
  const frequencies = new Set([...sameDevice.filter((use) => use.pin !== setting.pin).map((use) => use.hz), setting.hz])
  if (limit !== undefined && frequencies.size > limit) {
    warnings.push(`${device.name} runs at most ${limit} different PWM frequencies at once (one timer each); the project uses ${frequencies.size} (${[...frequencies].sort((a, b) => a - b).join(', ')} Hz), so the board refuses one. Pins with the same frequency share a timer.`)
  }
  return warnings
}

/** Warnings for a frequency about to be sent live (the command panel), against the project. */
export const frequencyWarnings = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], setup: readonly ActionStep[], actions: readonly ProjectAction[], setting: FrequencySetting): string[] =>
  warningsFor(catalog, devices, setup, actions, setting, frequencyUses(setup, actions))

/** The project's frequency steps, each checked against the rest: warnings on the default settings or the action that sets it. */
export const checkPwm = (catalog: DeviceCatalog, devices: readonly ProjectDevice[], setup: readonly ActionStep[], actions: readonly ProjectAction[]): UploadDiagnostic[] => {
  const uses = frequencyUses(setup, actions)
  const seen = new Set<string>()
  const diagnostics: UploadDiagnostic[] = []
  for (const use of uses) {
    for (const message of warningsFor(catalog, devices, setup, actions, use, uses.filter((other) => other !== use))) {
      const key = `${use.subjectId}|${message}`
      if (seen.has(key)) continue
      seen.add(key)
      diagnostics.push({ severity: 'warning', message, subjectId: use.subjectId })
    }
  }
  return diagnostics
}
