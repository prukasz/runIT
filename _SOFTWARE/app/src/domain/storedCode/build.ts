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
 * The board keeps two independent lists: the hardware config (1–3) and the program (4),
 * replayed in that order, each stored on its own (`StoredCode.config` / `.program`).
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

export interface HardwareConfig {
  /** No errors: `steps` is the complete hardware config list. */
  readonly ok: boolean
  readonly steps: readonly StoredCodeStep[]
  readonly diagnostics: readonly UploadDiagnostic[]
}

export interface StoredCode {
  /** Both slots built without errors: `steps` is the complete code. */
  readonly ok: boolean
  /** The whole code in replay order: `config` then `program`; empty unless `ok`. */
  readonly steps: readonly StoredCodeStep[]
  /** Hardware config slot: settings, device installs and default settings, other classes. Empty unless `configOk`. */
  readonly config: readonly StoredCodeStep[]
  readonly configOk: boolean
  /** Program slot: the VM loader frames. Empty unless `programOk`. */
  readonly program: readonly StoredCodeStep[]
  readonly programOk: boolean
  readonly diagnostics: readonly UploadDiagnostic[]
  /** The config's share of `diagnostics`. */
  readonly configDiagnostics: readonly UploadDiagnostic[]
  readonly vm: VmUploadPlan
}

/** Longest stored frame: the board's frame limit (CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX) less the seq byte live frames carry. */
export const storedFrameMax = (ids: SettingsIds): number => ids.limits.frameMax - 1

/** The hardware config slot alone: settings, device installs, default settings, extra frames. No program is compiled. */
export const buildHardwareConfig = (input: Omit<StoredCodeInput, 'project' | 'sections'>, ctx: StoredCodeContext): HardwareConfig => {
  const maxFrameBytes = storedFrameMax(ctx.ids)
  const settings = planSettingsUpload(ctx.commands, ctx.layout, ctx.ids, input.boardDefaults, input.settings)
  const devices = deviceInstallSteps(ctx.devices, input.devices ?? [])
  const setup = deviceSetupSteps(ctx.devices, input.devices ?? [], input.setup ?? [])
  const diagnostics: UploadDiagnostic[] = [...settings.diagnostics, ...devices.diagnostics, ...setup.diagnostics]

  const extra = input.extraFrames ?? []
  for (const [index, step] of extra.entries()) {
    if (step.frame.byteLength < 2 || step.frame.byteLength > maxFrameBytes) diagnostics.push({ severity: 'error', message: `Extra frame ${index} (${step.label || 'unlabelled'}) is ${step.frame.byteLength} bytes; stored frames are 2..${maxFrameBytes}.` })
    if (step.frame[0] === ctx.vm.classHeader && step.frame[1] === ctx.vm.packets.exec) diagnostics.push({ severity: 'error', message: `Extra frame ${index} is a VM exec command; autostart is a board setting, not stored code.` })
    else if (step.frame[0] === ctx.vm.classHeader) diagnostics.push({ severity: 'error', message: `Extra frame ${index} is a VM frame; the program slot holds those and the app builds them.` })
  }

  const ok = settings.ok && !diagnostics.some((entry) => entry.severity === 'error')
  return { ok, steps: ok ? [...settings.steps, ...devices.steps, ...setup.steps, ...extra] : [], diagnostics }
}

export const buildStoredCode = (input: StoredCodeInput, ctx: StoredCodeContext): StoredCode => {
  const hardware = buildHardwareConfig(input, ctx)
  const vm = planVmUpload(input.project, ctx.vm, { maxFrameBytes: storedFrameMax(ctx.ids), sections: input.sections })
  const programOk = vm.ok && !vm.diagnostics.some((entry) => entry.severity === 'error')
  // The subscribe step is part of the VM plan: subscriptions are state and replay like the rest.
  const config = hardware.steps
  const program: StoredCodeStep[] = programOk ? [...vm.steps] : []
  const ok = hardware.ok && programOk
  return { ok, steps: ok ? [...config, ...program] : [], config, configOk: hardware.ok, program, programOk, diagnostics: [...hardware.diagnostics, ...vm.diagnostics], configDiagnostics: hardware.diagnostics, vm }
}
