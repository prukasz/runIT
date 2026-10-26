import { useState } from 'react'
import { Bluetooth, Monitor, SlidersHorizontal, Waypoints, Zap } from 'lucide-react'
import { Badge, DeviceTile, PaletteSectionHeader, TreeSlab } from '@ui/components'
import type { SettingSection } from './settings'
import type { BoardSettingsState } from './useBoardSettings'
import { useErrors } from '../console/useConsole'

/*
 * The settings of the Project view's left panel, under the project (its landing page, App.tsx `ProjectSummary`): the board's,
 * a tab per service descriptor with settings (settings.ts), then the app's (the Live power display, App settings). The project
 * service's own settings (what the board does with the project at start) are on the landing page.
 */

/** The tab id of the project itself: the landing page. */
export const PROJECT_TAB = 'project'
/** The tab of the Live power display: what the right panel shows of the board's power (power/PowerDisplayScreen.tsx). */
export const POWER_TAB = 'live-power'

/** The tab of the app's own settings (appSettings/AppSettingsScreen.tsx). */
export const APP_TAB = 'app-settings'

/** The sections that have a tab of their own: every one but the project service's (shown on the Project tab). */
export const tabSections = (sections: readonly SettingSection[], projectKey: number | undefined): SettingSection[] =>
  sections.filter((s) => s.service.key !== projectKey)

const iconOf = (section: SettingSection) =>
  section.record?.editor === 'gatt' ? Bluetooth : section.record?.editor === 'streams' ? Waypoints : SlidersHorizontal

/** The section's settings say something the board does not have. */
const differs = (section: SettingSection, state: BoardSettingsState): boolean =>
  section.groups.some((g) => g.recordId !== undefined && state.formDiffers(g)) || section.tables.some((t) => state.tableDiffers(t))
  || (section.record?.editor === 'gatt' && state.gattDiffers()) || (section.record?.editor === 'streams' && state.streamsDiffer())

export function SettingsTabs({ sections, state, tab, onTab }: { sections: readonly SettingSection[]; state: BoardSettingsState; tab: string; onTab: (tab: string) => void }) {
  const [boardOpen, setBoardOpen] = useState(true)
  const [appOpen, setAppOpen] = useState(true)
  // The board's errors of a tab's commands (its router key), not read yet.
  const errors = useErrors()
  const unread = (key: number | undefined) => (key === undefined || !errors ? 0 : errors.console.unread(`router_key:${key}`))
  const errorBadge = (n: number) => n > 0 ? <Badge tone="danger" size="compact" title={`${n} errors not read (Console)`}>{n}</Badge> : undefined
  return (
    <div className="settings-tabs">
      <PaletteSectionHeader label="Board" count={sections.length} open={boardOpen} onToggle={() => setBoardOpen(!boardOpen)} />
      {boardOpen && sections.map((s) => (
        <TreeSlab key={s.service.id} icon={<DeviceTile size="small" icon={iconOf(s)} />} label={s.title} title={s.service.description} selected={tab === s.service.id}
          isError={unread(s.service.key) > 0}
          badges={<>{differs(s, state) && <Badge tone="warning" size="compact" title="Not on the board yet">changed</Badge>}{errorBadge(unread(s.service.key))}</>}
          onClick={() => onTab(s.service.id)} />
      ))}
      <PaletteSectionHeader label="App" count={2} open={appOpen} onToggle={() => setAppOpen(!appOpen)} />
      {appOpen && (
        <>
          <TreeSlab icon={<DeviceTile size="small" icon={Zap} />} label="Live power display" title="What the right panel shows of the board's power, and how it reads it" selected={tab === POWER_TAB} onClick={() => onTab(POWER_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Monitor} />} label="App settings" title="What belongs to the app on this computer: its look" selected={tab === APP_TAB} onClick={() => onTab(APP_TAB)} />
        </>
      )}
    </div>
  )
}
