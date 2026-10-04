import type { DeviceCatalog, InstallPinGroup } from '../../domain/descriptors'
import { BOARD_REF, specOf } from '../../domain/hardware'
import type { ConfigLink, WiringContext, WiringIssue } from '../../domain/hardware'
import { boardDeviceRef } from '../../domain/project'
import type { ActionStep, ProjectDevice } from '../../domain/project'
import { SET_MODE_CONTRACT } from '../useDevicesWorkspace'

/*
 * A wire from a device pin to a board GPIO is that device's pin ref in its install frame (`oe_pin` = device 0, pin 46).
 * Wiring fills the field while it is unset; once it holds something else the panel shows the difference and applies on
 * request, so a wire never overwrites a setting silently.
 */

export type LinkState = 'unset' | 'match' | 'differs'

export interface InstallLink {
  readonly link: ConfigLink
  readonly device: ProjectDevice
  readonly group: InstallPinGroup
  readonly state: LinkState
  /** What the install frame holds now (device ID and pin; undefined while the field is unset). */
  readonly current?: { readonly device: number; readonly pin: number }
}

export function installLink(link: ConfigLink, devices: readonly ProjectDevice[], catalog: DeviceCatalog): InstallLink | undefined {
  const device = devices.find((entry) => entry.id === link.device)
  const group = device ? catalog.type(device.type)?.pinGroups.find((entry) => entry.key === link.field) : undefined
  if (!device || !group?.deviceField || !group.pinField) return undefined
  const pin = device.install[group.pinField]
  const owner = device.install[group.deviceField]
  if (pin === undefined || pin === group.sentinel) return { link, device, group, state: 'unset' }
  const current = { device: owner ?? 0, pin }
  return { link, device, group, state: current.device === link.sysPin.device && current.pin === link.sysPin.pin ? 'match' : 'differs', current }
}

/** The install frame with the wire's pin ref filled in. */
export const installWith = (entry: Pick<InstallLink, 'device' | 'group' | 'link'>): Record<string, number> => ({
  ...entry.device.install,
  [entry.group.deviceField as string]: entry.link.sysPin.device,
  [entry.group.pinField as string]: entry.link.sysPin.pin,
})

/*
 * "Set it as an output / input": a pin that can be either, wired as the producer (or the consumer) of its net, needs
 * that mode. The fix is the pin's default mode in the project's setup, stored for the device and pin the wire's pin
 * stands for: a board terminal's sys_io pin, or a device pin's own pin number (the csv `bind_pin`).
 */
export interface ModeFix {
  /** Device reference (`board:<id>` or a user device's project ID) and its own pin number. */
  readonly ref: string
  readonly pin: number
  readonly mode: number
  readonly label: string
  /** The setup already holds this mode for the pin. */
  readonly applied: boolean
}

const MODE_SYMBOL: Readonly<Record<string, string>> = { 'set-output': 'SYS_IO_MODE_OUTPUT_PUSH_PULL', 'set-input': 'SYS_IO_MODE_INPUT' }

export function modeFix(issue: WiringIssue, context: WiringContext, catalog: DeviceCatalog, setup: readonly ActionStep[]): ModeFix | undefined {
  if ((issue.code !== 'set-output' && issue.code !== 'set-input') || !issue.device || !issue.pin) return undefined
  const spec = specOf(context, { device: issue.device, pin: issue.pin })
  const target = issue.device === BOARD_REF ? (spec?.sysPin ? { ref: boardDeviceRef(spec.sysPin.device), pin: spec.sysPin.pin } : undefined) : spec?.bindPin !== undefined ? { ref: issue.device, pin: spec.bindPin } : undefined
  const choice = catalog.ioModes.find((mode) => mode.symbol === MODE_SYMBOL[issue.code])
  if (!target || !choice) return undefined
  const current = setup.find((step) => step.device === target.ref && step.contract === SET_MODE_CONTRACT && step.values.pin === target.pin)?.values.mode
  return { ...target, mode: choice.value, label: choice.label, applied: current === choice.value }
}
