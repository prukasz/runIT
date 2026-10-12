import { useMemo } from 'react'
import type { DeviceCatalog } from './domain/descriptors'
import type { ActionStep, ObjectSection, ProjectDevice, ProjectDocument, ProjectSettings } from './domain/project'
import { settingsState } from './domain/settings'
import { buildHardwareConfig, buildStoredCode, crc32, encodeFrameList } from './domain/storedCode'
import { BOARD_DEFAULT_SETTINGS, storedCodeContext } from './useBoardCode'

const packSlot = (steps: readonly { frame: Uint8Array }[]) => {
  const bytes = encodeFrameList(steps.map((step) => step.frame))
  return { frames: steps.length, bytes, crc: crc32(bytes) }
}

/** The hardware config alone (no program is compiled): what the toolbar's save sends. */
export function useHardwareConfig({ settings, devices, deviceCatalog, setup, extraFrames }: {
  readonly settings: ProjectSettings
  readonly devices: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
  readonly setup: readonly ActionStep[]
  readonly extraFrames?: ProjectDocument['extraFrames']
}) {
  const config = useMemo(() => {
    const context = storedCodeContext()
    return buildHardwareConfig({ settings: settingsState(settings), boardDefaults: BOARD_DEFAULT_SETTINGS, devices, setup, extraFrames }, deviceCatalog ? { ...context, devices: deviceCatalog } : context)
  }, [settings, devices, deviceCatalog, setup, extraFrames])
  const slot = useMemo(() => packSlot(config.steps), [config])
  return { config, slot }
}

/** The project built into the frames a board replays at boot, with their packed bytes and CRC-32 (whole, and per slot: hardware config and program). */
export function useProjectCode({ project, sections, settings, devices, deviceCatalog, setup }: {
  readonly project: ProjectDocument
  readonly sections: readonly ObjectSection[]
  readonly settings: ProjectSettings
  readonly devices: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
  readonly setup: readonly ActionStep[]
}) {
  const code = useMemo(() => {
    const context = storedCodeContext()
    return buildStoredCode({ project, sections, settings: settingsState(settings), boardDefaults: BOARD_DEFAULT_SETTINGS, devices, setup, extraFrames: project.extraFrames }, deviceCatalog ? { ...context, devices: deviceCatalog } : context)
  }, [project, sections, settings, devices, deviceCatalog, setup])
  const bytes = useMemo(() => encodeFrameList(code.steps.map((step) => step.frame)), [code])
  const crc = useMemo(() => crc32(bytes), [bytes])
  // What each board slot holds: its list as packed bytes, with the CRC the board reports.
  const slots = useMemo(() => {
    return { config: packSlot(code.config), program: packSlot(code.program) }
  }, [code])
  return { code, bytes, crc, slots }
}
