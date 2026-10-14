import { useState } from 'react'
import { readStored, writeStored } from '../hooks/useStorage'
import { emptyBundle, parseBundle, serializeBundle, SETTINGS_ROUTERS } from '../domain/project'
import type { ProjectBundle } from '../domain/project'
import type { useCanvasWorkspace } from '../canvas/useCanvasWorkspace'
import type { useObjectTree } from '../objects/useObjectTree'
import type { DevicesWorkspace } from '../devices/useDevicesWorkspace'
import type { BleSettings } from '../settings/useBleSettings'
import type { ConnectorSettingsState } from '../settings/useConnectorSettings'
import type { RouterSettingsState } from '../settings/RouterSettingsView'

/*
 * The project as a file, as in the legacy app's Home view: New, Open, Download, and Restore previous (the project that New or Open
 * replaced, kept in browser storage; restoring swaps it with the current one, so a restore can be undone the same way).
 */

const PREVIOUS_KEY = 'runit.project.previous'

export interface ProjectWorkspaces {
  readonly objects: ReturnType<typeof useObjectTree>
  readonly canvas: ReturnType<typeof useCanvasWorkspace>
  readonly devices: DevicesWorkspace
  readonly ble: BleSettings
  readonly connectors: ConnectorSettingsState
  /** The settings pages of the routers the project keeps, by router symbol (`SETTINGS_ROUTERS`). */
  readonly routers: Readonly<Record<(typeof SETTINGS_ROUTERS)[number], RouterSettingsState>>
}

export interface FileResult {
  readonly ok: boolean
  readonly text: string
}

const readPrevious = (): ProjectBundle | undefined => readStored<ProjectBundle | undefined>(PREVIOUS_KEY, (raw) => parseBundle(raw), undefined)

export function useProjectFile(workspaces: ProjectWorkspaces) {
  const { objects, canvas, devices, ble, connectors, routers } = workspaces
  const [previous, setPrevious] = useState<ProjectBundle | undefined>(readPrevious)
  const [result, setResult] = useState<FileResult>()

  const current = (): ProjectBundle => ({
    project: objects.project,
    canvases: canvas.canvases,
    devices: {
      list: devices.devices,
      boardNames: devices.boardNames,
      ...(devices.pinAliases ? { pinAliases: devices.pinAliases } : {}),
      setup: devices.setup,
      actions: devices.actions,
    },
    settings: {
      ble: ble.profile,
      connectors: connectors.connectors,
      routers: Object.fromEntries(SETTINGS_ROUTERS.map((router) => [router, routers[router].values])),
    },
  })

  const apply = (bundle: ProjectBundle) => {
    objects.load(bundle.project)
    canvas.load(bundle.canvases)
    devices.load({
      devices: bundle.devices.list,
      boardNames: bundle.devices.boardNames,
      pinAliases: bundle.devices.pinAliases,
      setup: bundle.devices.setup,
      actions: bundle.devices.actions,
    })
    ble.replaceProfile(bundle.settings.ble)
    connectors.replaceConnectors([...bundle.settings.connectors])
    for (const router of SETTINGS_ROUTERS) routers[router].replace(bundle.settings.routers[router] ?? {})
  }

  /** Puts `bundle` in the editors and keeps the one it replaces for Restore previous. */
  const replaceWith = (bundle: ProjectBundle) => {
    const kept = current()
    writeStored(PREVIOUS_KEY, serializeBundle(kept))
    setPrevious(kept)
    apply(bundle)
  }

  const newProject = () => {
    replaceWith(emptyBundle())
    setResult({ ok: true, text: 'New project started. The one before can be restored.' })
  }

  const open = async (file: File | undefined) => {
    if (!file) return
    try {
      const bundle = parseBundle(await file.text())
      replaceWith(bundle)
      setResult({ ok: true, text: `Opened ${file.name}. The project before can be restored.` })
    } catch (error) {
      setResult({ ok: false, text: `${file.name}: ${error instanceof Error ? error.message : String(error)}` })
    }
  }

  const download = () => {
    const bundle = current()
    const url = URL.createObjectURL(new Blob([serializeBundle(bundle)], { type: 'application/json' }))
    const link = document.createElement('a')
    link.href = url
    link.download = `${bundle.project.name.trim() || 'project'}.json`
    link.click()
    setTimeout(() => URL.revokeObjectURL(url), 0)
    setResult({ ok: true, text: `Downloaded ${link.download}.` })
  }

  const restore = () => {
    if (!previous) return
    replaceWith(previous)
    setResult({ ok: true, text: 'The previous project is back (Restore previous again swaps them back).' })
  }

  const rename = (name: string) => objects.edit((project) => ({ ...project, name }), { key: 'project-name' })

  return { name: objects.project.name, current, newProject, open, download, restore, rename, canRestore: !!previous, previousName: previous?.project.name, result }
}

export type ProjectFile = ReturnType<typeof useProjectFile>
