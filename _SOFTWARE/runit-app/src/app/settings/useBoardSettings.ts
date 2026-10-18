import { useCallback, useEffect, useMemo, useState } from 'react'
import { serviceCall, Services } from '@backend'
import type { FieldValues, RunitLink, ServiceCall } from '@backend'
import type { DataStream, GattService, ProjectSettings, SettingsRow } from '../project/projectFile'
import type { useProject } from '../project/useProject'
import { decodeGattList, GATT, gattApply, sameGatt } from './gatt'
import type { LiveGattService } from './gatt'
import { groupValues, sameValues, settingSections, tableBytes, tableRows } from './settings'
import type { SettingGroup, SettingSection, SettingTable } from './settings'
import { decodeStreamsInfo, sameStreams, STREAMS, streamsApply } from './streams'

/*
 * The board's settings: what the board has now (read when the link comes up, and again on demand) and what the project keeps.
 *
 *   a setting with a record   the project's: a change goes into the project (undo, saved with it, written as its record at the
 *                             toolbar's Write, so it is applied at every start). Until the project changes it, the form shows
 *                             the board's. Apply now sends it to the board at once; Use the board's drops it from the project.
 *   a setting without         the board's own (it keeps it itself): changed in the form, then written at once (Write).
 */

type ProjectState = ReturnType<typeof useProject>

const message = (failure: unknown): string => (failure instanceof Error ? failure.message : String(failure))

/** What the board has now. */
export interface LiveSettings {
  readonly values: Readonly<Record<string, FieldValues>>
  readonly tables: Readonly<Record<string, readonly SettingsRow[]>>
  readonly gatt?: readonly LiveGattService[]
  readonly streams?: readonly DataStream[]
}

/** Where a setting's values come from: the project, the board (the project keeps none), or nowhere yet. */
export type SettingSource = 'project' | 'board' | 'none'

export function useBoardSettings(link: RunitLink, connected: boolean, project: ProjectState) {
  const services = useMemo(() => new Services(link), [link])
  const sections: readonly SettingSection[] = useMemo(() => settingSections(services.catalog), [services])
  const [live, setLive] = useState<LiveSettings>({ values: {}, tables: {} })
  /** Changes to the board's own settings (no record), not written yet. */
  const [edits, setEdits] = useState<Readonly<Record<string, FieldValues>>>({})
  const [errors, setErrors] = useState<Readonly<Record<string, string>>>({})
  const [busy, setBusy] = useState<string | undefined>()
  const settings = project.project.settings
  const setError = (key: string, text: string | undefined) => setErrors(({ [key]: _, ...rest }) => (text ? { ...rest, [key]: text } : rest))

  /** Reads one section from the board: its forms, its tables, and its record's table (GATT LIST, streams INFO). */
  const readSection = useCallback(async (section: SettingSection): Promise<boolean> => {
    let ok = true
    for (const g of section.groups) {
      try {
        const { value } = await services.call(g.service, g.read.id)
        setLive((l) => ({ ...l, values: { ...l.values, [g.key]: groupValues(g, value) } }))
        setError(g.key, undefined)
      } catch (failure) { ok = false; setError(g.key, `Reading: ${message(failure)}`) }
    }
    for (const t of section.tables) {
      try {
        const { value } = await services.call(t.service, t.read.id)
        setLive((l) => ({ ...l, tables: { ...l.tables, [t.key]: tableRows(t, value) } }))
        setError(t.key, undefined)
      } catch (failure) { ok = false; setError(t.key, `Reading: ${message(failure)}`) }
    }
    if (section.record) {
      const key = section.service.id
      try {
        if (section.record.editor === 'gatt') {
          const { data } = await services.call(GATT.service, GATT.list)
          setLive((l) => ({ ...l, gatt: decodeGattList(data) }))
        } else {
          const { data } = await services.call(STREAMS.service, STREAMS.info)
          setLive((l) => ({ ...l, streams: decodeStreamsInfo(data) }))
        }
        setError(key, undefined)
      } catch (failure) { ok = false; setError(key, `Reading: ${message(failure)}`) }
    }
    return ok
  }, [services])

  const read = useCallback(async (section?: SettingSection) => {
    let ok = true
    for (const s of section ? [section] : sections) ok = (await readSection(s)) && ok
    return ok
  }, [sections, readSection])

  // Read as soon as the link is up; what was shown of another board is dropped. A board that has just powered up may not answer
  // every section yet (its services still start), so the sections that failed are read again a few times.
  useEffect(() => {
    setLive({ values: {}, tables: {} })
    setEdits({})
    setErrors({})
    if (!connected) return
    let alive = true
    void (async () => {
      let pending = sections
      for (let attempt = 0; alive && pending.length > 0 && attempt < 5; attempt++) {
        if (attempt > 0) await new Promise((r) => setTimeout(r, 1500))
        if (!alive) return
        const failed: SettingSection[] = []
        for (const s of pending) if (!(await readSection(s))) failed.push(s)
        pending = failed
      }
    })()
    return () => { alive = false }
  }, [connected, sections, readSection])

  const editSettings = useCallback((change: (s: ProjectSettings) => ProjectSettings, key?: string) =>
    project.edit((p) => ({ ...p, settings: change(p.settings) }), key), [project])

  /** Sends commands in order; the section is read back after, so the forms show what the board took. */
  const send = useCallback(async (key: string, calls: readonly ServiceCall[], section: SettingSection) => {
    setBusy(key)
    try {
      for (const c of calls) await services.send(c)
      setError(key, undefined)
    } catch (failure) {
      setError(key, `Writing: ${message(failure)}`)
    } finally {
      setBusy(undefined)
    }
    await readSection(section)
  }, [services, readSection])

  const sectionOf = (key: string) => sections.find((s) => s.groups.some((g) => g.key === key) || s.tables.some((t) => t.key === key) || s.service.id === key)!

  return {
    sections, connected, busy, live,
    /** The catalog the commands come from. */
    sectionCatalog: services.catalog,
    error: (key: string) => errors[key],

    /* ---- forms ---- */
    formSource: (g: SettingGroup): SettingSource => (g.recordId !== undefined && settings.values[g.key] ? 'project' : live.values[g.key] ? 'board' : 'none'),
    formValues: (g: SettingGroup): FieldValues =>
      g.recordId !== undefined ? (settings.values[g.key] ?? live.values[g.key] ?? {}) : { ...live.values[g.key], ...edits[g.key] },
    /** The form says something the board does not have. */
    formDiffers: (g: SettingGroup): boolean => {
      const board = live.values[g.key]
      const mine = g.recordId !== undefined ? settings.values[g.key] : edits[g.key] && { ...board, ...edits[g.key] }
      return board !== undefined && mine !== undefined && !sameValues(g.layout, mine, board)
    },
    changeForm: (g: SettingGroup, values: FieldValues) => {
      if (g.recordId !== undefined) editSettings((s) => ({ ...s, values: { ...s.values, [g.key]: values } }), `setting:${g.key}`)
      else setEdits((e) => ({ ...e, [g.key]: values }))
    },
    /** Sends the form's values to the board now. */
    applyForm: (g: SettingGroup) => {
      const values = g.recordId !== undefined ? settings.values[g.key] : { ...live.values[g.key], ...edits[g.key] }
      if (!values) return
      void send(g.key, [formCall(services, g, values)], sectionOf(g.key)).then(() => { if (g.recordId === undefined) setEdits(({ [g.key]: _, ...rest }) => rest) })
    },
    /** The project keeps none (the board's stays): drops the project's values, or the unwritten changes. */
    dropForm: (g: SettingGroup) => {
      if (g.recordId !== undefined) editSettings(({ values: { [g.key]: _, ...values }, ...s }) => ({ ...s, values }))
      else setEdits(({ [g.key]: _, ...rest }) => rest)
    },

    /* ---- tables ---- */
    tableSource: (t: SettingTable): SettingSource => (settings.tables[t.key] ? 'project' : live.tables[t.key] ? 'board' : 'none'),
    tableRowsOf: (t: SettingTable): readonly SettingsRow[] => settings.tables[t.key] ?? live.tables[t.key] ?? [],
    tableDiffers: (t: SettingTable): boolean => {
      const [mine, board] = [settings.tables[t.key], live.tables[t.key]]
      return mine !== undefined && board !== undefined && JSON.stringify(tableBytes(mine)) !== JSON.stringify(tableBytes(board))
    },
    changeTable: (t: SettingTable, rows: readonly SettingsRow[]) => editSettings((s) => ({ ...s, tables: { ...s.tables, [t.key]: rows } }), `setting:${t.key}`),
    applyTable: (t: SettingTable) => {
      const rows = settings.tables[t.key]
      if (rows) void send(t.key, [tableCall(services, t, rows)], sectionOf(t.key))
    },
    dropTable: (t: SettingTable) => editSettings(({ tables: { [t.key]: _, ...tables }, ...s }) => ({ ...s, tables })),

    /* ---- GATT ---- */
    gattSource: (): SettingSource => (settings.gatt ? 'project' : live.gatt ? 'board' : 'none'),
    gattServices: (): readonly GattService[] => settings.gatt ?? (live.gatt ?? []).filter((s) => !s.locked),
    gattDiffers: (): boolean => settings.gatt !== undefined && live.gatt !== undefined && !sameGatt(settings.gatt, live.gatt.filter((s) => !s.locked)),
    changeGatt: (services: readonly GattService[]) => editSettings((s) => ({ ...s, gatt: services }), 'setting:gatt'),
    applyGatt: (section: SettingSection) => {
      if (settings.gatt && live.gatt) void send(section.service.id, gattApply(services.catalog, live.gatt, settings.gatt), section)
    },
    dropGatt: () => editSettings(({ gatt: _, ...s }) => s),

    /* ---- data streams ---- */
    streamsSource: (): SettingSource => (settings.streams ? 'project' : live.streams ? 'board' : 'none'),
    streamsOf: (): readonly DataStream[] => settings.streams ?? live.streams ?? [],
    streamsDiffer: (): boolean => settings.streams !== undefined && live.streams !== undefined && !sameStreams(settings.streams, live.streams),
    changeStreams: (streams: readonly DataStream[]) => editSettings((s) => ({ ...s, streams }), 'setting:streams'),
    applyStreams: (section: SettingSection) => {
      if (settings.streams && live.streams) void send(section.service.id, streamsApply(services.catalog, live.streams, settings.streams), section)
    },
    dropStreams: () => editSettings(({ streams: _, ...s }) => s),

    /**
     * Reads a section from the board and shows what it has: the project's values for it are dropped (Undo brings them back),
     * so the forms are the board's. (The first read, when the link comes up, leaves the project's alone.)
     */
    read: (section?: SettingSection) => {
      void read(section).then(() => {
        const mine = (s: SettingSection) => ({
          keys: [...s.groups, ...s.tables].map((x) => x.key), gatt: s.record?.editor === 'gatt', streams: s.record?.editor === 'streams',
        })
        const parts = (section ? [section] : sections).map(mine)
        editSettings((s) => {
          const values = { ...s.values }
          const tables = { ...s.tables }
          for (const p of parts) for (const k of p.keys) { delete values[k]; delete tables[k] }
          const { gatt, streams, ...rest } = s
          return { ...rest, values, tables, ...(parts.some((p) => p.gatt) ? {} : gatt ? { gatt } : {}), ...(parts.some((p) => p.streams) ? {} : streams ? { streams } : {}) }
        })
        setEdits({})
      })
    },
  }
}

/** The write of a form: the fields the write takes (a read may answer more). */
const formCall = (services: Services, g: SettingGroup, values: FieldValues): ServiceCall => {
  const known = new Set(g.layout.fields.map((f) => f.id))
  return serviceCall(services.catalog, g.service, g.write.id, Object.fromEntries(Object.entries(values).filter(([id]) => known.has(id))))
}

/** The write of a table: its one bytes field, every row's cells. */
const tableCall = (services: Services, t: SettingTable, rows: readonly SettingsRow[]): ServiceCall =>
  serviceCall(services.catalog, t.service, t.write.id, { [t.write.args!.fields[0]!.id]: tableBytes(rows) })

export type BoardSettingsState = ReturnType<typeof useBoardSettings>
