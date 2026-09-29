import { useMemo } from 'react'
import type { DeviceCatalog } from './domain/descriptors'
import type { ActionStep, ObjectSection, ProjectDevice, ProjectDocument, ProjectSettings } from './domain/project'
import { settingsState } from './domain/settings'
import { buildStoredCode, crc32, encodeFrameList } from './domain/storedCode'
import { BOARD_DEFAULT_SETTINGS, storedCodeContext } from './useBoardCode'

/** The project built into the frames a board replays at boot, with their packed bytes and CRC-32. */
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
  return { code, bytes, crc }
}
