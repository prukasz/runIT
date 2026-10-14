import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { PointerEvent, KeyboardEvent } from 'react'
import { Bluetooth, BluetoothConnected, BluetoothOff, Code2, Cpu, FolderOpen, LayoutGrid, Moon, PanelLeft, PanelRight, Play, Redo2, Rows3, ScrollText, Square, Sun, Download, Trash2, Undo2, Upload, Zap } from 'lucide-react'
import { DetailsPanel, ExplorerPanel, MainScreen, WorkspaceShell } from './layout'
import { readStored, usePersistedChoice, usePersistedFlag, writeStored } from './hooks/useStorage'
import { BlePanel } from './BlePanel'
import { CanvasEditor } from './canvas/CanvasEditor'
import { useCanvasWorkspace } from './canvas/useCanvasWorkspace'
import { BlockInfo } from './blocks/BlockInfo'
import { BlockPalette } from './blocks/BlockPalette'
import { SegmentedSwitch } from './components/SegmentedSwitch/SegmentedSwitch'
import { BlockDetails } from './blocks/BlockDetails'
import { descriptorOf } from './blocks/blockModel'
import { findBlock } from './domain/canvas'
import { addObject } from './domain/project'
import { ObjectDetails } from './objects/ObjectDetails'
import { ObjectTreeEditor } from './objects/ObjectTreeEditor'
import { ObjectTreePalette } from './objects/ObjectTreePalette'
import { useObjectTree } from './objects/useObjectTree'
import { BleDetails, BleSettingsEditor, BleSettingsPalette } from './settings/BleSettingsView'
import { ConnectorDetails, ConnectorSettingsEditor, ConnectorSettingsPalette } from './settings/ConnectorSettingsView'
import { RouterSettingsEditor, useRouterSettings } from './settings/RouterSettingsView'
import { SettingsGroupSidebar, SETTINGS_GROUPS } from './settings/SettingsOverview'
import { ProjectFilePage, ProjectFilePalette } from './project/ProjectFileView'
import { useProjectFile } from './project/useProjectFile'
import { DeviceDetails, DevicesEditor, DevicesPalette, useDevicesWorkspace } from './devices'
import { useBleSettings } from './settings/useBleSettings'
import { useConnectorSettings } from './settings/useConnectorSettings'
import { useSettingsSend } from './settings/useSettingsSend'
import type { BoardSettings } from './domain/settings'
import { RunitBleLink } from './ble'
import type { LinkState } from './ble'
import './styles/theme.css'
import './App.css'
import './styles/status.css'  // after App.css: the status colors must win over the plain button styles

const PALETTES = [['Variables', 'Variables'], ['Blocks', 'Blocks']] as const
const COLLAPSED_WIDTH = 54
const MIN_PANEL = 200
const MAX_PANEL = 500
const MAX_RIGHT_PANEL = 520
const MIN_CENTER = 420
const KEY_STEP = 16

const viewWidth = (): number => (typeof window === 'undefined' ? 1920 : window.innerWidth)

const clampWidth = (width: number, other: number, max = MAX_PANEL) =>
  Math.round(Math.min(Math.max(MIN_PANEL, viewWidth() - other - MIN_CENTER), max, Math.max(MIN_PANEL, width)))

function usePanelWidth(key: string, fallback: number) {
  const [width, setWidth] = useState(() => readStored(key, (raw) => (Number.isFinite(Number(raw)) ? Number(raw) : undefined), fallback))
  return [width, (next: number) => { setWidth(next); writeStored(key, String(next)) }] as const
}

function ResizeHandle({ side, label, width, onResize, onStart, onEnd }: {
  side: 'left' | 'right'
  label: string
  width: number
  onResize: (clientX: number) => void
  onStart: () => void
  onEnd: () => void
}) {
  const [active, setActive] = useState(false)
  const end = () => { setActive(false); onEnd() }
  const key = (event: KeyboardEvent) => {
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return
    event.preventDefault()
    const grow = (event.key === 'ArrowRight') === (side === 'left') ? KEY_STEP : -KEY_STEP
    onResize(side === 'left' ? width + grow : viewWidth() - (width + grow))
  }
  return (
    <div
      className={`panel-resize-handle ${side}-resize-handle ${active ? 'active' : ''}`}
      role="separator"
      aria-label={label}
      aria-orientation="vertical"
      tabIndex={0}
      onPointerDown={(event: PointerEvent) => { event.currentTarget.setPointerCapture(event.pointerId); setActive(true); onStart() }}
      onPointerMove={(event) => { if (active) onResize(event.clientX) }}
      onPointerUp={end}
      onPointerCancel={end}
      onKeyDown={key}
    />
  )
}

/** The screens, as tabs along the top of the left panel. */
const VIEWS = [
  { name: 'Project', icon: FolderOpen, title: 'Project' },
  { name: 'Code', icon: Code2, title: 'Code' },
  { name: 'Devices', icon: Cpu, title: 'Devices' },
] as const

function ViewTabs({ view, onView }: { view: (typeof VIEWS)[number]['name']; onView: (view: (typeof VIEWS)[number]['name']) => void }) {
  return (
    <div className="view-menu" role="tablist" aria-label="Current view">
      {VIEWS.map(({ name, icon: Icon, title }) => (
        <button key={name} role="tab" aria-selected={view === name} title={title} aria-label={title} className={view === name ? 'selected' : ''} onClick={() => onView(name)}>
          <Icon aria-hidden="true" />
        </button>
      ))}
    </div>
  )
}

const LINK_VIEW: Record<LinkState, { tone: string; Icon: typeof Bluetooth; title: string }> = {
  connected: { tone: 'status-ok', Icon: BluetoothConnected, title: 'Bluetooth: connected' },
  waiting: { tone: 'status-lost', Icon: BluetoothOff, title: 'Bluetooth: connection lost' },
  connecting: { tone: 'status-warn', Icon: Bluetooth, title: 'Bluetooth: connecting…' },
  idle: { tone: 'status-warn', Icon: Bluetooth, title: 'Bluetooth: not connected' },
  unavailable: { tone: 'status-warn', Icon: BluetoothOff, title: 'This browser has no Web Bluetooth' },
}

export function App() {
  const link = useMemo(() => new RunitBleLink(), [])
  const [linkState, setLinkState] = useState<LinkState>(link.state)
  const [device, setDevice] = useState<string | undefined>()
  const [auto, setAuto] = usePersistedFlag('runit.autoReconnect', true)
  const [bleOpen, setBleOpen] = useState(false)
  useEffect(() => {
    link.setAutoReconnect(auto)
  }, [link, auto])
  const restored = useRef(false)
  useEffect(() => {
    const off = link.onState((state, name) => { setLinkState(state); setDevice(name) })
    // Once per page load (StrictMode runs effects twice): the remembered board, no chooser.
    if (auto && !restored.current) {
      restored.current = true
      void link.restore().catch((error) => console.warn('automatic connect failed:', error))
    }
    return off
    // Only on load: later changes of `auto` are applied above.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [link])
  const { tone, Icon: LinkIcon, title: linkTitle } = LINK_VIEW[linkState]
  const [leftOpen, setLeftOpen] = useState(true)
  const [rightOpen, setRightOpen] = useState(false)
  const [leftWidth, setLeftWidth] = usePanelWidth('runit.leftWidth', 260)
  const [rightWidth, setRightWidth] = usePanelWidth('runit.rightWidth.v2', 440)
  const [theme, setTheme] = usePersistedChoice('runit.theme', ['dark', 'light'] as const, 'dark')
  const openBle = () => {
    if (bleOpen && rightOpen) return setBleOpen(false)
    setBleOpen(true)
    setRightOpen(true)
    setRightWidth(clampWidth(MAX_RIGHT_PANEL, leftOpen ? leftNow : COLLAPSED_WIDTH, MAX_RIGHT_PANEL))
  }
  const rightButtons = (
    <>
      <button aria-label="Run" title="Run"><Play aria-hidden="true" /></button>
      <button aria-label="Stop" title="Stop"><Square aria-hidden="true" /></button>
      <button className={`ble-toggle ${tone} ${bleOpen && rightOpen ? 'selected' : ''}`} aria-label="Bluetooth" title={linkTitle} aria-pressed={bleOpen && rightOpen} onClick={openBle}>
              <LinkIcon aria-hidden="true" />
            </button>
    </>
  )
  const objects = useObjectTree()
  const canvas = useCanvasWorkspace()
  const [view, setView] = usePersistedChoice('runit.shell.view', ['Project', 'Code', 'Devices'] as const, 'Project')
  // Project settings: the group picked in the left panel decides what the middle shows.
  const [settingsGroup, setSettingsGroup] = usePersistedChoice('runit.settings.group', SETTINGS_GROUPS, 'all')
  const bleSettings = useBleSettings(view === 'Project' && settingsGroup === 'BLE')
  const connectorSettings = useConnectorSettings()
  const logSettings = useRouterSettings('ROUTER_KEY_LOG')
  const powerSettings = useRouterSettings('ROUTER_KEY_POWER')
  const sendSnapshot = useMemo(() => ({ ble: bleSettings.profile, connectors: connectorSettings.connectors }), [bleSettings.profile, connectorSettings.connectors])
  const sendRouters = useMemo(() => [logSettings, powerSettings], [logSettings, powerSettings])
  const { replaceProfile } = bleSettings
  const { replaceConnectors } = connectorSettings
  const { replace: replaceLog } = logSettings
  const { replace: replacePower } = powerSettings
  const onSynced = useCallback((board: BoardSettings) => {
    replaceProfile(board.ble)
    replaceConnectors(board.connectors)
    replaceLog(board.routers.ROUTER_KEY_LOG ?? {})
    replacePower(board.routers.ROUTER_KEY_POWER ?? {})
  }, [replaceProfile, replaceConnectors, replaceLog, replacePower])
  const devices = useDevicesWorkspace()
  const { records: deviceImages, foreign: deviceForeign, saved: devicesSaved, synced: devicesSynced } = devices
  const deviceRecords = useMemo(() => ({ images: deviceImages, foreign: deviceForeign, saved: devicesSaved, synced: devicesSynced }), [deviceImages, deviceForeign, devicesSaved, devicesSynced])
  const settingsSend = useSettingsSend(link, linkState, sendSnapshot, sendRouters, onSynced, deviceRecords)
  const pickSettingsGroup = (group: typeof SETTINGS_GROUPS[number]) => setSettingsGroup(group)
  const projectFile = useProjectFile({ objects, canvas, devices, ble: bleSettings, connectors: connectorSettings, routers: { ROUTER_KEY_LOG: logSettings, ROUTER_KEY_POWER: powerSettings } })
  const [mode, setMode] = usePersistedChoice('runit.code.mode', ['manage', 'canvas'] as const, 'manage')
  // The right panel follows the selection: the block picked on the canvas, else the object picked in the tree.
  const selectedBlock = mode === 'canvas' && canvas.selectedIds.length === 1 ? findBlock(canvas.canvases, canvas.selectedIds[0]!)?.block : undefined
  // The right panel slides out while something is selected (a block, an object of the tree, the Bluetooth panel) and away when the
  // selection is cleared; the toggle still opens or closes it by hand until the selection changes again. Clicking the empty
  // canvas clears the block and the object, so the panel goes.
  const settingsDetailed = view === 'Project' && ((settingsGroup === 'BLE' && !!bleSettings.selectedItem && bleSettings.selectedItem.kind !== 'general') || settingsGroup === 'Connectors')
  const deviceDetailed = view === 'Devices' && devices.selection.kind === 'device'
  const hasContent = bleOpen || (view === 'Code' && (!!selectedBlock || !!objects.selectedId)) || settingsDetailed || deviceDetailed
  useEffect(() => { setRightOpen(hasContent) }, [hasContent, view, selectedBlock?.id, objects.selectedId, bleOpen, settingsGroup, bleSettings.selectedId, devices.selection])
  // The block (or device block operation) picked in the palette, as `encodePlacement` words it: read about, then dragged onto the canvas.
  const [pick, setPick] = useState<string | undefined>()
  const [palette, setPalette] = usePersistedChoice('runit.palette', ['Variables', 'Blocks'] as const, 'Variables')
  const [resizing, setResizing] = useState(false)

  const leftNow = clampWidth(leftWidth, rightOpen ? rightWidth : COLLAPSED_WIDTH)
  const rightNow = clampWidth(rightWidth, leftOpen ? leftWidth : COLLAPSED_WIDTH, MAX_RIGHT_PANEL)

  return (
    <WorkspaceShell className={`theme-${theme} ${resizing ? 'is-resizing' : ''} ${objects.linkingParentId !== undefined ? 'is-linking' : ''}`}>
      {objects.linkingParentId !== undefined && <div className="object-link-backdrop" onClick={() => objects.setLinkingParentId(undefined)} />}
      <ExplorerPanel open={leftOpen} width={leftNow}>
        {leftOpen && (
          <ResizeHandle side="left" label="Resize left panel" width={leftNow} onStart={() => setResizing(true)} onEnd={() => setResizing(false)}
            onResize={(x) => setLeftWidth(clampWidth(x, rightOpen ? rightNow : COLLAPSED_WIDTH))} />
        )}
        <div className="side-panel-header left-strip">
          {leftOpen && <ViewTabs view={view} onView={setView} />}
          <button className="panel-toggle" aria-label={leftOpen ? 'Collapse left panel' : 'Expand left panel'} onClick={() => setLeftOpen(!leftOpen)}>
            <PanelLeft aria-hidden="true" />
          </button>
        </div>
        {leftOpen && view === 'Devices' && <div className="side-panel-body"><DevicesPalette workspace={devices} /></div>}
        {leftOpen && view === 'Project' && (
          <div className="side-panel-body settings-sidebar-wrapper">
            {settingsGroup === 'all' && <ProjectFilePalette file={projectFile} onPickSettings={pickSettingsGroup} />}
            {settingsGroup === 'BLE' && <BleSettingsPalette workspace={bleSettings} onBack={() => setSettingsGroup('all')} />}
            {settingsGroup === 'Connectors' && <ConnectorSettingsPalette workspace={connectorSettings} onBack={() => setSettingsGroup('all')} />}
            {settingsGroup === 'Logging' && <SettingsGroupSidebar title="Logging" onBack={() => setSettingsGroup('all')} />}
            {settingsGroup === 'Power' && <SettingsGroupSidebar title="Power" onBack={() => setSettingsGroup('all')} />}
          </div>
        )}
        {leftOpen && view === 'Code'
          ? (
            <div className="side-panel-body">
              <div className="palette-switch"><SegmentedSwitch label="Palette" value={palette} options={PALETTES} onChange={setPalette} /></div>
              {palette === 'Variables' ? <ObjectTreePalette workspace={objects} /> : <BlockPalette selected={pick} onSelect={setPick} onDragType={(type) => { canvas.paletteDrag.current = type }} />}
            </div>
          )
          : !leftOpen && <div className="side-panel-icons"><ViewTabs view={view} onView={(next) => { setView(next); setLeftOpen(true) }} /></div>}
      </ExplorerPanel>

      <div className="center-column">
        <MainScreen
          label="Main screen"
          toolbar={
            <>
              {view === 'Project' && settingsGroup === 'BLE' && (
                <>
                  <button aria-label="Undo" title="Undo (Ctrl+Z)" disabled={!bleSettings.canUndo} onClick={bleSettings.undo}><Undo2 aria-hidden="true" /></button>
                  <button aria-label="Redo" title="Redo (Ctrl+Y)" disabled={!bleSettings.canRedo} onClick={bleSettings.redo}><Redo2 aria-hidden="true" /></button>
                </>
              )}
              {view === 'Code' && (
                <>
                <button aria-label={mode === 'canvas' ? 'Show the objects and blocks' : 'Show the canvas'} title={mode === 'canvas' ? 'Objects and blocks' : 'Canvas'} className={mode === 'canvas' ? 'selected' : ''} onClick={() => setMode(mode === 'canvas' ? 'manage' : 'canvas')}>
                  {mode === 'canvas' ? <Rows3 aria-hidden="true" /> : <LayoutGrid aria-hidden="true" />}
                </button>
                <button aria-label="Undo" title="Undo (Ctrl+Z)" disabled={mode === 'canvas' ? !canvas.canUndo : !objects.canUndo} onClick={mode === 'canvas' ? canvas.undo : objects.undo}><Undo2 aria-hidden="true" /></button>
                <button aria-label="Redo" title="Redo (Ctrl+Y)" disabled={mode === 'canvas' ? !canvas.canRedo : !objects.canRedo} onClick={mode === 'canvas' ? canvas.redo : objects.redo}><Redo2 aria-hidden="true" /></button>
                </>
              )}
              <span className="toolbar-spacer" />
              {view === 'Project' && (
                <>
                  {settingsSend.status.message && <span className={`settings-send-note is-${settingsSend.status.phase}`} role="status">{settingsSend.status.message}</span>}
                  <button aria-label="Read the settings from the board" title={settingsSend.connected ? 'Read the settings stored on the board' : 'Connect the board to read the settings'} disabled={!settingsSend.connected || settingsSend.status.phase === 'sending'} onClick={() => void settingsSend.sync()}>
                    <Download aria-hidden="true" />
                  </button>
                  <button aria-label="Send the settings to the board" title={settingsSend.connected ? 'Send the settings to the board' : 'Connect the board to send the settings'} disabled={!settingsSend.connected || settingsSend.status.phase === 'sending'} onClick={() => void settingsSend.send()}>
                    <Upload aria-hidden="true" />
                  </button>
                </>
              )}
              {view === 'Code' && mode === 'canvas' && (
                <button aria-label="Delete" title="Delete the selected blocks (Delete)" disabled={canvas.selectedIds.length === 0} onClick={() => canvas.deleteBlocks(canvas.selectedIds)}><Trash2 aria-hidden="true" /></button>
              )}
              <button aria-label={theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'} title={theme === 'dark' ? 'Light theme' : 'Dark theme'} onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}>
                {theme === 'dark' ? <Sun aria-hidden="true" /> : <Moon aria-hidden="true" />}
              </button>
            </>
          }
        >
          {view === 'Project' ? (
            settingsGroup === 'BLE' ? <BleSettingsEditor workspace={bleSettings} />
              : settingsGroup === 'Connectors' ? <ConnectorSettingsEditor workspace={connectorSettings} bleProfile={bleSettings.profile} />
              : settingsGroup === 'Logging' ? <RouterSettingsEditor workspace={logSettings} title="Logging" icon={<ScrollText aria-hidden="true" />} />
              : settingsGroup === 'Power' ? <RouterSettingsEditor workspace={powerSettings} title="Power" icon={<Zap aria-hidden="true" />} />
              : <ProjectFilePage file={projectFile} />
          ) : view === 'Devices' ? <DevicesEditor workspace={devices} /> : mode === 'canvas' ? <CanvasEditor workspace={canvas} showGrid project={objects.project} onBackgroundClick={() => objects.select(null)} /> : palette === 'Variables' ? <ObjectTreeEditor workspace={objects} /> : <BlockInfo placement={pick} />}
        </MainScreen>
      </div>

      <DetailsPanel open={rightOpen} expanded={false} style={rightOpen ? { width: rightNow } : undefined}>
        {rightOpen && (
          <ResizeHandle side="right" label="Resize right panel" width={rightNow} onStart={() => setResizing(true)} onEnd={() => setResizing(false)}
            onResize={(x) => setRightWidth(clampWidth(viewWidth() - x, leftOpen ? leftNow : COLLAPSED_WIDTH, MAX_RIGHT_PANEL))} />
        )}
        <div className="side-panel-header right-header">
          <button className="panel-toggle" aria-label={rightOpen ? 'Collapse right panel' : 'Expand right panel'} onClick={() => setRightOpen(!rightOpen)}>
            <PanelRight aria-hidden="true" />
          </button>
          {rightOpen && <div className="header-icons">{rightButtons}</div>}
        </div>
        {rightOpen
          ? (
            <div className="side-panel-body">
              {bleOpen ? (
                <BlePanel link={link} state={linkState} device={device} auto={auto} onAuto={setAuto} />
              ) : view === 'Devices' ? (
                <DeviceDetails workspace={devices} link={link} linkState={linkState} />
              ) : view === 'Project' ? (
                settingsGroup === 'BLE' ? <BleDetails workspace={bleSettings} connectors={connectorSettings.connectors} onJumpToConnector={(key) => { connectorSettings.select(key); setSettingsGroup('Connectors') }} /> : settingsGroup === 'Connectors' ? <ConnectorDetails workspace={connectorSettings} onJumpToBle={() => setSettingsGroup('BLE')} /> : null
              ) : selectedBlock ? (
                <BlockDetails key={selectedBlock.id} block={selectedBlock} blocks={canvas.active?.blocks ?? []} descriptor={descriptorOf(selectedBlock.type)} project={objects.project} selectedObjectId={objects.selectedId ?? undefined} onClearSelectedObject={() => objects.select(null)} onCreateVariable={(node) => objects.edit((current) => addObject(current, null, node))} onChange={(change) => canvas.updateBlock(selectedBlock.id, change)} />
              ) : (
                <ObjectDetails workspace={objects} />
              )}
            </div>
          )
          : <div className="side-panel-icons">{rightButtons}</div>}
      </DetailsPanel>
    </WorkspaceShell>
  )
}
