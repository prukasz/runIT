import { widgetFromDefinition } from '../domain/controller'
import type { CustomWidgetFile, WidgetType } from '../domain/controller'

/*
 * Imported widgets: a .js file is loaded as a module and its default export
 * checked (`widgetFromDefinition`). It is the user's own code, trusted like
 * the app's: it runs in the page, so it can do anything the page can.
 */

/** Loads one file's widget: its type, or what went wrong (the file does not run, or does not describe a widget). */
export const loadWidgetFile = async (file: CustomWidgetFile): Promise<{ readonly type: WidgetType } | { readonly error: string }> => {
  try {
    // A data: URL, not a blob: it works wherever a module can be imported, and `import()` caches nothing across different sources.
    const module = await import(/* @vite-ignore */ `data:text/javascript;charset=utf-8,${encodeURIComponent(file.source)}`) as { default?: unknown }
    return widgetFromDefinition(module.default, file.name)
  } catch (failure) {
    return { error: failure instanceof Error ? failure.message : String(failure) }
  }
}

export interface LoadedWidgets {
  readonly types: ReadonlyMap<string, WidgetType>
  /** File name → why it did not load. */
  readonly errors: ReadonlyMap<string, string>
}

/** All the stored files; a kind two files claim belongs to the first. */
export const loadWidgetFiles = async (files: readonly CustomWidgetFile[]): Promise<LoadedWidgets> => {
  const types = new Map<string, WidgetType>()
  const errors = new Map<string, string>()
  for (const file of files) {
    const loaded = await loadWidgetFile(file)
    if ('error' in loaded) errors.set(file.name, loaded.error)
    else if (types.has(loaded.type.kind)) errors.set(file.name, `the widget "${loaded.type.kind}" is already provided by ${types.get(loaded.type.kind)!.file}`)
    else types.set(loaded.type.kind, loaded.type)
  }
  return { types, errors }
}
