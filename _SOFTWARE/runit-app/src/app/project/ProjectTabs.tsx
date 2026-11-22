import { useState } from 'react'
import { Cable, Cpu, FileCog, Network } from 'lucide-react'
import { DeviceTile, PaletteSectionHeader, TreeSlab } from '@ui/components'

/*
 * The tabs of the Project view's left panel, under the project (its landing page, App.tsx `ProjectSummary`): what the board is
 * made of and linked to. The settings of the board's services and of the app are in the Settings view (settings/SettingsTabs.tsx).
 */

/** The tab id of the project itself: the landing page. */
export const PROJECT_TAB = 'project'

/** The tab of the project's other boards, linked to this one over BLE (boards/BoardsScreen.tsx). */
export const BOARDS_TAB = 'boards'

/** The tab of the board's profile as a file: Open and Save (devices/BoardProfileScreen.tsx). */
export const PROFILE_TAB = 'board-profile'

/** The tab of the board's devices, the board editor (devices/DevicesScreen.tsx over the board's profile). */
export const BOARD_DEVICES_TAB = 'board-devices'

/** The tab of the board's connections: its providers, what they carry, their sessions and dead-man (routing/ConnectionsScreen.tsx). */
export const CONNECTIONS_TAB = 'connections'

export function ProjectTabs({ tab, onTab }: { tab: string; onTab: (tab: string) => void }) {
  const [boardOpen, setBoardOpen] = useState(true)
  return (
    <div className="settings-tabs">
      <PaletteSectionHeader label="Board" count={4} open={boardOpen} onToggle={() => setBoardOpen(!boardOpen)} />
      {boardOpen && (
        <>
          <TreeSlab icon={<DeviceTile size="small" icon={FileCog} />} label="Board profile" title="The board's profile as a file: open one (the bundled boards' defaults) or save this one" selected={tab === PROFILE_TAB} onClick={() => onTab(PROFILE_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Cpu} />} label="Board devices" title="The devices the board has installed, with their settings and what they do at every start" selected={tab === BOARD_DEVICES_TAB} onClick={() => onTab(BOARD_DEVICES_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Cable} />} label="Connections" title="Providers (Bluetooth, UART, the console), what each carries, their sessions and the dead-man" selected={tab === CONNECTIONS_TAB} onClick={() => onTab(CONNECTIONS_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Network} />} label="Boards" title="Other boards this one leads over Bluetooth: found, linked, given their ids" selected={tab === BOARDS_TAB} onClick={() => onTab(BOARDS_TAB)} />
        </>
      )}
    </div>
  )
}
