import { useState } from 'react'
import { ArrowLeftRight, Cable, Cpu, Network } from 'lucide-react'
import { DeviceTile, PaletteSectionHeader, TreeSlab } from '@ui/components'

/*
 * The tabs of the Project view's left panel, under the project (its landing page, App.tsx `ProjectSummary`): what the board is
 * made of and linked to. The settings of the board's services and of the app are in the Settings view (settings/SettingsTabs.tsx).
 */

/** The tab id of the project itself: the landing page. */
export const PROJECT_TAB = 'project'

/** The tab of the project's other boards, linked to this one over BLE (boards/BoardsScreen.tsx). */
export const BOARDS_TAB = 'boards'

/** The tab of the board wired to this one by a UART, reached through it (remote/RemoteScreen.tsx); the main board's only. */
export const REMOTE_TAB = 'remote'

/**
 * The tab of the board's profile, the board editor: the profile as a file (devices/BoardProfileScreen.tsx) while no device is
 * chosen, else the chosen device (devices/DevicesScreen.tsx over the profile); the left panel lists the profile and its devices.
 */
export const BOARD_TAB = 'board'

/** The tab of the board's connections: its providers, what they carry, their sessions and dead-man (routing/ConnectionsScreen.tsx). */
export const CONNECTIONS_TAB = 'connections'

export function ProjectTabs({ tab, onTab, remote }: { tab: string; onTab: (tab: string) => void; remote: boolean }) {
  const [boardOpen, setBoardOpen] = useState(true)
  return (
    <div className="settings-tabs">
      <PaletteSectionHeader label="Board" count={remote ? 4 : 3} open={boardOpen} onToggle={() => setBoardOpen(!boardOpen)} />
      {boardOpen && (
        <>
          <TreeSlab icon={<DeviceTile size="small" icon={Cpu} />} label="Board profile" title="The board's profile: its devices, their settings and what they do at every start; open, save, read and write it" selected={tab === BOARD_TAB} onClick={() => onTab(BOARD_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Cable} />} label="Connections" title="Providers (Bluetooth, UART, the console), what each carries, their sessions and the dead-man" selected={tab === CONNECTIONS_TAB} onClick={() => onTab(CONNECTIONS_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Network} />} label="Boards" title="Other boards this one leads over Bluetooth: found, linked, given their ids" selected={tab === BOARDS_TAB} onClick={() => onTab(BOARDS_TAB)} />
          {remote && <TreeSlab icon={<DeviceTile size="small" icon={ArrowLeftRight} />} label="Create remote" title="A second board wired to this one by a UART: connected through it and shown in its place" selected={tab === REMOTE_TAB} onClick={() => onTab(REMOTE_TAB)} />}
        </>
      )}
    </div>
  )
}
