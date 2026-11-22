import { useState } from 'react'
import { Cable, Cpu, FileCog, Network, Share2 } from 'lucide-react'
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

/** The tab for creating and inspecting data providers (routing/ProvidersScreen.tsx). */
export const PROVIDERS_TAB = 'providers'

/** The tab for visualizing and wiring the data matrix + sessions (routing/DataMatrixScreen.tsx). */
export const DATA_MATRIX_TAB = 'data-matrix'

export function ProjectTabs({ tab, onTab }: { tab: string; onTab: (tab: string) => void }) {
  const [boardOpen, setBoardOpen] = useState(true)
  return (
    <div className="settings-tabs">
      <PaletteSectionHeader label="Board" count={5} open={boardOpen} onToggle={() => setBoardOpen(!boardOpen)} />
      {boardOpen && (
        <>
          <TreeSlab icon={<DeviceTile size="small" icon={FileCog} />} label="Board profile" title="The board's profile as a file: open one (the bundled boards' defaults) or save this one" selected={tab === PROFILE_TAB} onClick={() => onTab(PROFILE_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Cpu} />} label="Board devices" title="The devices the board has installed, with their settings and what they do at every start" selected={tab === BOARD_DEVICES_TAB} onClick={() => onTab(BOARD_DEVICES_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Cable} />} label="Providers" title="Data providers (BLE, UART, Console): create, inspect and remove transport endpoints" selected={tab === PROVIDERS_TAB} onClick={() => onTab(PROVIDERS_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Share2} />} label="Data matrix" title="Interactive graph of modules, sessions and providers: attach sessions and wire ports live" selected={tab === DATA_MATRIX_TAB} onClick={() => onTab(DATA_MATRIX_TAB)} />
          <TreeSlab icon={<DeviceTile size="small" icon={Network} />} label="Boards" title="Other boards this one leads over Bluetooth: found, linked, given their ids" selected={tab === BOARDS_TAB} onClick={() => onTab(BOARDS_TAB)} />
        </>
      )}
    </div>
  )
}
