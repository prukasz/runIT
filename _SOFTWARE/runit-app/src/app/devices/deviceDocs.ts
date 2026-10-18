/*
 * What the bundle holds for a device (templates/generate_bundle.py): its manual (`generated/manuals/<id>/manual.md` and the
 * pictures beside it) and a copy of its own code (`generated/sources/<id>/`). Keyed by the device type's `id`, which the
 * descriptor names; a device without a folder has no page yet.
 */

const MANUALS = import.meta.glob('@generated/manuals/*/manual*.md', { eager: true, query: '?raw', import: 'default' }) as Record<string, string>
const PICTURES = import.meta.glob('@generated/manuals/*/*.{png,jpg,jpeg,gif,svg,webp}', { eager: true, query: '?url', import: 'default' }) as Record<string, string>
const SOURCES = import.meta.glob('@generated/sources/*/*.{c,h}', { eager: true, query: '?raw', import: 'default' }) as Record<string, string>

/** The file name a key ends with, in the folder `<folder>/<id>/`: `[id, file]` when it is that file. */
const inFolder = (key: string, folder: string): readonly [string, string] | undefined => {
  const match = key.match(new RegExp(`${folder}/([^/]+)/([^/]+)$`))
  return match ? [match[1]!, match[2]!] : undefined
}

/** The languages a manual comes in: `manual.md` is English, `manual.<lang>.md` the others. */
export type ManualLanguage = 'en' | 'pl'

/**
 * The device's manual in a language as written (markdown). When that language is not there, the English one, and `fallback` is
 * true; undefined when the device has no manual at all.
 */
export const manualOf = (id: string, language: ManualLanguage): { readonly text: string; readonly language: ManualLanguage; readonly fallback: boolean } | undefined => {
  const manuals = new Map<ManualLanguage, string>()
  for (const [key, text] of Object.entries(MANUALS)) {
    const found = inFolder(key, 'manuals')
    if (!found || found[0] !== id) continue
    const lang = found[1].match(/^manual\.([a-z]{2})\.md$/)?.[1] as ManualLanguage | undefined
    manuals.set(lang ?? 'en', text)
  }
  const text = manuals.get(language)
  if (text !== undefined) return { text, language, fallback: false }
  const english = manuals.get('en')
  return english === undefined ? undefined : { text: english, language: 'en', fallback: true }
}

/** The pictures of a device's manual, by file name, as URLs the page can show. */
export const picturesOf = (id: string): Readonly<Record<string, string>> =>
  Object.fromEntries(Object.entries(PICTURES).flatMap(([key, url]) => {
    const found = inFolder(key, 'manuals')
    return found && found[0] === id ? [[found[1], url]] : []
  }))

/** The device's own code, as the bundle copied it: its `.h` and `.c` files, by name. */
export const sourcesOf = (id: string): readonly { readonly name: string; readonly text: string }[] =>
  Object.entries(SOURCES)
    .flatMap(([key, text]) => {
      const found = inFolder(key, 'sources')
      return found && found[0] === id ? [{ name: found[1], text }] : []
    })
    .sort((a, b) => a.name.localeCompare(b.name))
