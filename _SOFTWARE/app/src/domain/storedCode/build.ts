import type { BleLayout, CommandCatalog, DeviceCatalog, VmCatalog } from '../descriptors'
import { deviceInstallSteps, deviceSetupSteps } from '../devices'
import type { ActionStep, ObjectSection, ProjectDevice, ProjectDocument } from '../project'
import { planSettingsUpload, planVmUpload } from '../upload'
import type { SettingsIds, SettingsState, UploadDiagnostic, VmUploadPlan } from '../upload'
import type { StoredCodeStep } from './frameList'

/*
 * App project → the stored code: every frame that configures a board from its
 * defaults, in replay order (app/docs/03_records_as_storage.md §5):
 *   1. BLE services / characteristics and data connectors (the settings
 *      planner from board defaults to the project's settings);
 *   2. the user's devices (install packets), then the devices' default
 *      settings (pin modes, levels … as contract calls);
 *   3. frames of classes without an editor yet, kept as they are;
 *   4. the VM program: open, objects, values and links, subscribe.
 * Frames are sized for the replay path (no link, no seq byte): up to the
 * board's frame limit less one. VM exec frames are never part of it: autostart
 * is the board's `prj_opts` setting.
 */

export interface StoredCodeContext {
  readonly vm: VmCatalog
  readonly commands: CommandCatalog
  readonly layout: BleLayout
  readonly ids: SettingsIds
  readonly devices: DeviceCatalog
}

export interface StoredCodeInput {
  readonly project: ProjectDocument
  /** Object sections after the user's tree (block outputs …). */
  readonly sections?: readonly ObjectSection[]
  /** The project's BLE and connector settings. */
  readonly settings: SettingsState
  /** What a board holds after a restart without stored code. */
  readonly boardDefaults: SettingsState
  /** The user's devices, installed after the settings. */
  readonly devices?: readonly ProjectDevice[]
  /** Default settings of any device, run after the installs. */
  readonly setup?: readonly ActionStep[]
  /** Frames of classes the app has no editor for yet (power, events …), stored unchanged. */
  readonly extraFrames?: readonly StoredCodeStep[]
}

export interface StoredCode {
  /** No errors: `steps` is the complete code. */
  readonly ok: boolean
  readonly steps: readonly StoredCodeStep[]
  readonly diagnostics: readonly UploadDiagnostic[]
  readonly vm: VmUploadPlan
}

/** Longest stored frame: the board's frame limit (CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX) less the seq byte live frames carry. */
export const storedFrameMax = (ids: SettingsIds): number => ids.limits.frameMax - 1

export const buildStoredCode = (input: StoredCodeInput, ctx: StoredCodeContext): StoredCode => {
  const maxFrameBytes = storedFrameMax(ctx.ids)
  const settings = planSettingsUpload(ctx.commands, ctx.layout, ctx.ids, input.boardDefaults, input.settings)
  const vm = planVmUpload(input.project, ctx.vm, { maxFrameBytes, sections: input.sections })
  const devices = deviceInstallSteps(ctx.devices, input.devices ?? [])
  const setup = deviceSetupSteps(ctx.devices, input.devices ?? [], input.setup ?? [])
  const diagnostics: UploadDiagnostic[] = [...settings.diagnostics, ...devices.diagnostics, ...setup.diagnostics, ...vm.diagnostics]

  const extra = input.extraFrames ?? []
  for (const [index, step] of extra.entries()) {
    if (step.frame.byteLength < 2 || step.frame.byteLength > maxFrameBytes) diagnostics.push({ severity: 'error', message: `Extra frame ${index} (${step.label || 'unlabelled'}) is ${step.frame.byteLength} bytes; stored frames are 2..${maxFrameBytes}.` })
    if (step.frame[0] === ctx.vm.classHeader && step.frame[1] === ctx.vm.packets.exec) diagnostics.push({ severity: 'error', message: `Extra frame ${index} is a VM exec command; autostart is a board setting, not stored code.` })
  }

  const ok = settings.ok && vm.ok && !diagnostics.some((entry) => entry.severity === 'error')
  // The subscribe step is part of the VM plan: subscriptions are state and replay like the rest.
  const steps: StoredCodeStep[] = ok ? [...settings.steps, ...devices.steps, ...setup.steps, ...extra, ...vm.steps] : []
  return { ok, steps, diagnostics, vm }
}
