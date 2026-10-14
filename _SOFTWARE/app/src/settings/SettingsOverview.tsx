import { ArrowLeft, ChevronRight, Network, Radio, ScrollText, Zap } from 'lucide-react'
import type { ReactNode } from 'react'
import './Settings.css'

/** The groups of the Project screen's settings (the board's general BLE options are in Bluetooth LE): the left panel lists them, picking one changes what the middle shows. */
export type SettingsGroup = 'all' | 'BLE' | 'Connectors' | 'Logging' | 'Power'

export const SETTINGS_GROUPS = ['all', 'BLE', 'Connectors', 'Logging', 'Power'] as const

const CATEGORIES = [
  { group: 'BLE', title: 'Bluetooth LE', sub: 'GATT services & characteristics', icon: Radio },
  { group: 'Connectors', title: 'Data Connectors', sub: 'Streams, BLE & UART wiring', icon: Network },
  { group: 'Logging', title: 'Logging', sub: 'Log level & where it goes', icon: ScrollText },
  { group: 'Power', title: 'Power', sub: 'Power budget & USB source', icon: Zap },
] as const

export function SettingsCategories({ onPick }: { onPick: (group: SettingsGroup) => void }) {
  return (
    <div className="settings-overview-menu">
      <div className="settings-menu-heading">Settings Categories</div>
      {CATEGORIES.map(({ group, title, sub, icon: Icon }) => (
        <button key={group} type="button" className="settings-category-btn" onClick={() => onPick(group)}>
          <Icon className="settings-cat-icon" aria-hidden="true" />
          <div className="settings-cat-info">
            <span className="settings-cat-title">{title}</span>
            <span className="settings-cat-sub">{sub}</span>
          </div>
          <ChevronRight aria-hidden="true" />
        </button>
      ))}
    </div>
  )
}

/** The left panel of a group that has no list of its own: a way back, and its name. */
export function SettingsGroupSidebar({ title, onBack, children }: { title: string; onBack: () => void; children?: ReactNode }) {
  return (
    <div className="settings-general-sidebar">
      <div className="ble-palette-back-bar">
        <button type="button" className="settings-back-btn" onClick={onBack} title="Back to the project">
          <ArrowLeft aria-hidden="true" />
          <span>Project</span>
        </button>
      </div>
      <div className="settings-overview-menu">
        <div className="settings-menu-heading">{title}</div>
        {children}
      </div>
    </div>
  )
}
