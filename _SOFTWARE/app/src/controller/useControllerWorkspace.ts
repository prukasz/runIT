import { useEffect, useState } from 'react'
import { addCustomFile, addWidget, duplicateWidget, EMPTY_CONTROLLER, importedWidgetTypes, linkWidget, MAX_WIDGET_SOURCE, parseController, placeWidget, removeCustomFile, removeWidget, renameWidget, setImportedWidgetTypes, setOption, setPad } from '../domain/controller'
import type { Controller, WidgetOption } from '../domain/controller'
import { useUndoHistory } from '../hooks/useUndoHistory'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { loadWidgetFile, loadWidgetFiles } from './customWidgets'
import type { LoadedWidgets } from './customWidgets'

/*
 * The Remote view's state: the widgets (undo / redo, autosaved in the browser
 * and saved with the project), which one is selected, and whether the layout
 * is shown alone (full screen). Whether the interface is live is the debug
 * session's: the stage reads it where it is drawn.
 */

const STORAGE_KEY = 'runit.controller'

const loadStored = (): Controller => readStored<Controller>(STORAGE_KEY, (raw) => parseController(JSON.parse(raw)), EMPTY_CONTROLLER)

export function useControllerWorkspace() {
  const history = useUndoHistory<Controller>(loadStored, { coalesceMs: 800 })
  const [selectedId, setSelectedId] = useState<string>()
  const [fullscreen, setFullscreen] = useState(false)
  const controller = history.present
  usePersistEffect(STORAGE_KEY, controller)

  // The imported widget files stored in the controller, loaded as modules (asynchronously) and handed to the model's registry.
  const [loaded, setLoaded] = useState<LoadedWidgets & { readonly ready: boolean }>({ types: new Map(), errors: new Map(), ready: false })
  useEffect(() => {
    let current = true
    void loadWidgetFiles(controller.custom ?? []).then((result) => {
      if (!current) return
      setImportedWidgetTypes(result.types)
      setLoaded({ ...result, ready: true })
    })
    return () => { current = false }
  }, [controller.custom])

  const edit = (change: (current: Controller) => Controller, key?: string) => history.record(change(history.current()), key ? { key } : {})
  const selected = controller.widgets.find((widget) => widget.id === selectedId)

  return {
    controller,
    selected,
    selectedId: selected?.id,
    select: (id: string | undefined) => setSelectedId(id),
    fullscreen,
    setFullscreen,
    canUndo: history.canUndo,
    canRedo: history.canRedo,
    undo: history.undo,
    redo: history.redo,
    /** A widget from the palette: at a grid cell (a drop) or the first free spot; it becomes the selection. */
    add: (kind: string, at?: { x: number; y: number }) => {
      const { controller: next, widget } = addWidget(history.current(), kind, at)
      history.record(next)
      setSelectedId(widget.id)
    },
    place: (id: string, box: { x: number; y: number; w: number; h: number }) => edit((current) => placeWidget(current, id, box)),
    remove: (id: string) => {
      edit((current) => removeWidget(current, id))
      setSelectedId((current) => (current === id ? undefined : current))
    },
    duplicate: (id: string) => {
      const { controller: next, widget } = duplicateWidget(history.current(), id)
      history.record(next)
      if (widget) setSelectedId(widget.id)
    },
    rename: (id: string, name: string) => edit((current) => renameWidget(current, id, name), `name:${id}`),
    link: (id: string, slot: string, variableId: string | undefined) => edit((current) => linkWidget(current, id, slot, variableId)),
    pad: (id: string, pad: string | undefined) => edit((current) => setPad(current, id, pad)),
    option: (id: string, key: string, value: WidgetOption) => edit((current) => setOption(current, id, key, value), `option:${id}:${key}`),
    /** The imported widget files: whether they have loaded, and why one did not. */
    widgetsReady: loaded.ready,
    widgetErrors: loaded.errors,
    /** Import a widget file (its text); resolves with what is wrong with it, or nothing when it is in. */
    importWidget: async (name: string, source: string): Promise<string | undefined> => {
      if (source.length > MAX_WIDGET_SOURCE) return `${name}: the file is larger than ${MAX_WIDGET_SOURCE / 1000} KB.`
      const result = await loadWidgetFile({ name, source })
      if ('error' in result) return `${name}: ${result.error}`
      const other = importedWidgetTypes().find((type) => type.kind === result.type.kind && type.file !== name)
      if (other) return `${name}: the widget "${result.type.kind}" is already imported from ${other.file}.`
      history.record(addCustomFile(history.current(), { name, source }))
      return undefined
    },
    removeImported: (name: string) => edit((current) => removeCustomFile(current, name)),
    /** A project was opened or replaced. */
    load: (next: Controller | undefined) => {
      history.replace(next ?? EMPTY_CONTROLLER, 'reset')
      setSelectedId(undefined)
    },
  }
}

export type ControllerWorkspace = ReturnType<typeof useControllerWorkspace>
