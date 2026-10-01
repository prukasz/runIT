import { useEffect, useMemo, useRef, useState } from 'react'
import { usePersistedChoice } from './hooks/useStorage'
import { DetailsPanel, ExplorerPanel, MainScreen, WorkspaceShell } from './layout'
import { ArrowLeft, ArrowLeftRight, BoxSelect, Bug, Check, ChevronRight, ClipboardPaste, Code2, Copy, Cpu, Gamepad2, Grid2X2, House, Info, ListTree, Magnet, Moon, Network, PanelLeftClose, PanelLeftOpen, PanelRightClose, PanelRightOpen, Play, Plug, Radio, Redo2, ScrollText, Settings2, Sliders, Square, SquareTerminal, Sun, Trash2, Undo2, Variable, X } from 'lucide-react'
import { ObjectDetails, ObjectTreeEditor, ObjectTreePalette, useObjectTreeWorkspace } from './ObjectTreeWorkspace'
import { LogSettingsEditor, useLogSettingsWorkspace } from './LogSettingsWorkspace'
import { BleDetails, BleSettingsEditor, BleSettingsPalette, useBleSettingsWorkspace } from './BleSettingsWorkspace'
import { useBleDeviceConnection } from './useBleDeviceConnection'
import { BleConnectPanel } from './BleConnectPanel'
import {
  DataConnectorsEditor,
  DataConnectorsPalette,
  DataConnectorDetails,
  useDataConnectorsWorkspace,
} from './DataConnectorsWorkspace'
import { ProgramPanel } from './ProgramPanel'
import { DebugHost } from './debug/DebugHost'
import { DebugPanel } from './debug/DebugPanel'
import { DebugSettings } from './debug/DebugSettings'
import { ProjectFilePage, ProjectFilePalette } from './ProjectFile'
import { BoardCanvas } from './devices/BoardCanvas'
import { DeviceDetails, DevicesEditor, DevicesPalette, useDevicesWorkspace } from './devices'
import { BlockManager, BlockUses } from './canvas/blocks/BlockManager'
import { BlockDetails, blockDiagnostics, BlockPalette, CanvasEditor, recoveredCanvases, useCanvasWorkspace } from './canvas'
import { OBJECT_DRAG_TYPE, objectKindDragType } from './domain/canvas'
import { programBlocks } from './domain/canvas'
import { compileProgram } from './domain/compiler'
import { runitVmCatalog } from './domain/descriptors'
import { allDevices, pinDisplayLabel } from './domain/devices'
import type { ResolveConsoleReference } from './ConsoleReference'
import CommandConsole from './CommandConsole'
import DiagnosticsConsole from './DiagnosticsConsole'
import { sampleProject } from './sampleProject'
import { useSettingsSync } from './useSettingsSync'
import { useBoardCode } from './useBoardCode'
import { addObject, createProject, parseProject, serializeProject } from './domain/project'
import type { ProjectDocument } from './domain/project'
import type { ProjectSettings } from './domain/project'
import { refreshSettings, settingsFromState, settingsState } from './domain/settings'
import type { RecoveredCode } from './domain/storedCode'
import './styles/theme.css'
import './Console.css'
import './App.css'

/** Browser storage key of the project a replace (new, open, import, recover) put aside. */
const PREVIOUS_KEY = 'runit.project.previous'

const views = [
  { name: 'Home', icon: House },
  { name: 'Settings', icon: Settings2 },
  { name: 'Board', icon: Cpu },
  { name: 'Code', icon: Code2 },
  { name: 'Remote', icon: Gamepad2 },
] as const

const details = [
  { name: 'Info', icon: Info },
  { name: 'Run', icon: Play },
  { name: 'Stop', icon: Square },
  { name: 'Connect', icon: Plug },
  { name: 'Debug', icon: Bug },
]

export type SettingsGroup = 'all' | 'BLE' | 'General' | 'Connectors' | 'Logging'
const codePalettes = ['Blocks', 'Variables'] as const
type CodePalette = typeof codePalettes[number]
type CodeMode = 'manage' | 'canvas'

export default function App() {
  const [view, setView] = usePersistedChoice('runit.shell.view', views.map(({ name }) => name), 'Home')
  const [settingsGroup, setSettingsGroup] = usePersistedChoice<SettingsGroup>('runit.settings.group', ['all', 'BLE', 'General', 'Connectors', 'Logging'], 'BLE')
  // The Code view's palette and main view (canvas or the variables manager) are remembered.
  const [codePalette, setCodePalette] = usePersistedChoice<CodePalette>('runit.code.palette', ['Variables', 'Blocks'], 'Variables')
  const [codeMode, setCodeMode] = usePersistedChoice<CodeMode>('runit.code.mode', ['manage', 'canvas'], 'manage')
  const [boardMode, setBoardMode] = usePersistedChoice<'manage' | 'canvas'>('runit.board.mode', ['manage', 'canvas'], 'manage')
  const [showCanvasGrid, setShowCanvasGrid] = useState(true)
  // View / Manage with the Blocks palette: the block type whose description the main screen shows.
  const [manageBlockKey, setManageBlockKey] = usePersistedChoice<string>('runit.code.manageBlock', runitVmCatalog().blocks.map((type) => type.key), runitVmCatalog().blocks[0]?.key ?? '')
  const [leftOpen, setLeftOpen] = useState(true)
  const [leftWidth, setLeftWidth] = useState(260)
  const [detail, setDetail] = useState('Info')
  const [rightOpen, setRightOpen] = useState(false)
  const [rightWidth, setRightWidth] = useState(320)
  const [rightExpanded, setRightExpanded] = useState(false)
  const prevRightWidthRef = useRef<number | undefined>(undefined)
  const [terminalOpen, setTerminalOpen] = useState(false)
  const [terminalHeight, setTerminalHeight] = useState(220)
  const [terminalTab, setTerminalTab] = useState<'Commands' | 'Errors & logs'>('Commands')
  const [pinDestination, setPinDestination] = useState<{ ref: string; pin: number; request: number }>()
  const [resizing, setResizing] = useState<'left' | 'right' | 'bottom' | null>(null)
  const [theme, setTheme] = usePersistedChoice<'dark' | 'light'>('runit-shell-theme', ['dark', 'light'], 'dark')
  const objectWorkspace = useObjectTreeWorkspace(() => { setRightOpen(true); setDetail('Info') })
  const bleWorkspace = useBleSettingsWorkspace((id) => {
    if (id === 'general') {
      setRightOpen(false)
    } else {
      setRightOpen(true)
      setDetail('Info')
    }
  })
  const connectorsWorkspace = useDataConnectorsWorkspace(() => { setRightOpen(true); setDetail('Info') })
  const logWorkspace = useLogSettingsWorkspace()
  const devicesWorkspace = useDevicesWorkspace(() => { setRightOpen(true); setDetail('Info') })
  const canvasWorkspace = useCanvasWorkspace(() => { setRightOpen(true); setDetail('Info') })
  const compiledReferences = useMemo(() => terminalOpen ? compileProgram(objectWorkspace.project, runitVmCatalog(), { maxFrameBytes: 240, sections: objectWorkspace.sections, blocks: programBlocks(canvasWorkspace.canvases) }) : undefined, [terminalOpen, objectWorkspace.project, objectWorkspace.sections, canvasWorkspace.canvases])
  // User services must be named when the board is picked, or Web Bluetooth hides them.
  const userServiceUuids = useMemo(
    () => bleWorkspace.profile.services.map((service) => Number.parseInt(service.uuid.replace(/^0x/i, ''), 16)).filter((uuid) => Number.isInteger(uuid) && uuid > 0),
    [bleWorkspace.profile.services],
  )
  const bleConnection = useBleDeviceConnection(userServiceUuids)
  const projectSettings = useMemo<ProjectSettings>(() => ({ ble: bleWorkspace.profile, connectors: connectorsWorkspace.connectors, logs: logWorkspace.logs }), [bleWorkspace.profile, connectorsWorkspace.connectors, logWorkspace.logs])
  const settingsTarget = useMemo(() => settingsState(projectSettings), [projectSettings])
  const boardCode = useBoardCode(bleConnection)
  const settingsSync = useSettingsSync(bleConnection, boardCode, settingsTarget)
  const settingsDirty = settingsSync.pending > 0 || !settingsSync.plan.ok
  const applySettings = async () => {
    if (!bleConnection.isConnected) {
      bleConnection.notifyDisconnectedAttempt()
      setRightOpen(true)
      setDetail('Connect')
      return
    }
    // Keep the result in view: the Connect panel shows what was sent or why not.
    setRightOpen(true)
    setDetail('Connect')
    if (await settingsSync.apply()) bleWorkspace.markApplied()
  }
  const { selectedId, remove, linkingParentId, setLinkingParentId } = objectWorkspace
  // What the compiler says about each block, against the project's objects.
  const canvasDiagnostics = useMemo(() => blockDiagnostics(objectWorkspace.project, canvasWorkspace.canvases, runitVmCatalog(), 240), [objectWorkspace.project, canvasWorkspace.canvases])

  // The project the debug build is made from: objects and the canvases' blocks.
  const debugProject = useMemo(() => ({ ...objectWorkspace.project, canvases: canvasWorkspace.canvases }), [objectWorkspace.project, canvasWorkspace.canvases])

  // The project file: objects from the object workspace, settings from the BLE and connector workspaces.
  const loadSettings = (settings: ProjectSettings) => {
    bleWorkspace.load(settings.ble)
    connectorsWorkspace.load(settings.connectors)
    logWorkspace.load(settings.logs)
  }
  /** Every workspace takes its part of a project document. */
  const applyProject = (project: ProjectDocument) => {
    objectWorkspace.load(project)
    loadSettings(refreshSettings(project.settings))
    devicesWorkspace.load({ devices: project.devices ?? [], deviceAliases: project.deviceAliases, pinAliases: project.pinAliases, actions: project.actions ?? [], setup: project.setup ?? [] })
    canvasWorkspace.load(project.canvases ?? [])
  }
  const currentProject = (): ProjectDocument => ({ ...objectWorkspace.project, settings: projectSettings, devices: devicesWorkspace.devices, deviceAliases: devicesWorkspace.deviceAliases, pinAliases: devicesWorkspace.pinAliases, actions: devicesWorkspace.actions, setup: devicesWorkspace.setup, canvases: canvasWorkspace.canvases })
  // Replacing the project never asks: what was there is kept in the browser and comes back with Restore previous.
  const [hasPrevious, setHasPrevious] = useState(() => { try { return !!localStorage.getItem(PREVIOUS_KEY) } catch { return false } })
  const keepPrevious = () => {
    try {
      localStorage.setItem(PREVIOUS_KEY, serializeProject(currentProject()))
      setHasPrevious(true)
    } catch { /* storage unavailable: nothing to keep */ }
  }
  const restorePrevious = () => {
    try {
      const text = localStorage.getItem(PREVIOUS_KEY)
      if (!text) return
      keepPrevious()
      applyProject(parseProject(text))
    } catch (cause) {
      window.alert(`Can't restore: ${cause instanceof Error ? cause.message : String(cause)}`)
    }
  }
  const newProject = () => {
    keepPrevious()
    applyProject(createProject('Untitled'))
  }
  const loadSample = () => {
    keepPrevious()
    applyProject(sampleProject())
  }
  const openProject = async (file: File | undefined) => {
    if (!file) return
    try {
      const opened = parseProject(await file.text())
      keepPrevious()
      applyProject(opened)
    } catch (cause) {
      window.alert(`Can't open ${file.name}: ${cause instanceof Error ? cause.message : String(cause)}`)
    }
  }
  const saveProject = () => {
    const project = currentProject()
    let text: string
    try {
      text = serializeProject(project)
    } catch (cause) {
      window.alert(`Can't save: ${cause instanceof Error ? cause.message : String(cause)}`)
      return
    }
    const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }))
    const link = document.createElement('a')
    link.href = url
    link.download = `${project.name || 'project'}.runit.json`
    link.click()
    setTimeout(() => URL.revokeObjectURL(url), 0)
  }
  /** Code read back from a board becomes the project (recovery); the BLE general settings and the actions stay. */
  const recoverProject = (recovered: RecoveredCode, autostart: boolean) => {
    keepPrevious()
    objectWorkspace.load({ ...recovered.project, autostart, extraFrames: recovered.extraFrames }, recovered.sections)
    loadSettings(settingsFromState(recovered.settings, projectSettings))
    devicesWorkspace.load({ devices: recovered.devices, deviceAliases: devicesWorkspace.deviceAliases, pinAliases: devicesWorkspace.pinAliases, actions: devicesWorkspace.actions, setup: recovered.setup })
    // The recovered program is the code now: its blocks on a canvas of their own.
    canvasWorkspace.load(recoveredCanvases(recovered.blocks))
  }
  const resizeActive = useRef(false)
  const leftResizeActive = useRef(false)
  const bottomResizeActive = useRef(false)

  const isCodeVariables = view === 'Code' && codePalette === 'Variables'
  const isSettingsBle = view === 'Settings' && settingsGroup === 'BLE'
  const isBoard = view === 'Board'
  const isCanvas = view === 'Code' && codeMode === 'canvas'

  const openBlockAt = (canvasId: string, blockId: string) => {
    setView('Code')
    setCodeMode('canvas')
    setRightOpen(true)
    setDetail('Info')
    canvasWorkspace.reveal(canvasId, blockId)
  }
  const openBlock = (id: string) => {
    const canvas = canvasWorkspace.canvases.find((entry) => entry.blocks.some((block) => block.id === id))
    if (canvas) openBlockAt(canvas.id, id)
  }
  const openDevice = (ref: string) => {
    setPinDestination(undefined)
    setView('Board')
    setLeftOpen(true)
    devicesWorkspace.select({ kind: 'device', ref })
  }
  const openDevicePin = (ref: string, pin: number) => {
    setPinDestination((current) => ({ ref, pin, request: (current?.request ?? 0) + 1 }))
    setView('Board')
    setLeftOpen(true)
    devicesWorkspace.select({ kind: 'device', ref })
  }
  const resolveConsoleReference: ResolveConsoleReference = (field, value, siblings) => {
    if (!Number.isSafeInteger(value) || value < 0) return undefined
    const destination = (label: string, open: () => void) => ({ label, open: () => { setTerminalOpen(false); open() } })
    const rawName = field.toLowerCase()
    const name = rawName.split(':').at(-1)!
    const devices = allDevices(devicesWorkspace.catalog, devicesWorkspace.devices)
    const deviceId = [siblings.device_id, siblings.dev_id, siblings.device].find((entry) => typeof entry === 'number') as number | undefined
    if (['device_id', 'dev_id', 'device'].includes(name) || name === 'id' && rawName.includes('device')) {
      const device = devices.find((entry) => entry.deviceId === value)
      return device && destination(device.name, () => openDevice(device.ref))
    }
    if (['pin', 'pin_num', 'io_num'].includes(name) && deviceId !== undefined) {
      const device = devices.find((entry) => entry.deviceId === deviceId)
      return device && destination(`${device.name}: ${pinDisplayLabel(devicesWorkspace.catalog, device.ref, value)}`, () => openDevicePin(device.ref, value))
    }
    if (['object_id', 'obj_id', 'object_idx', 'obj_idx', 'object', 'variable_id', 'variable'].includes(name)) {
      const placed = compiledReferences?.objects.objects[value]
      if (!placed) return undefined
      if (placed.section === 'blocks') {
        const blockId = placed.node.id.split(':')[0]!
        return destination(placed.node.name || blockId, () => openBlock(blockId))
      }
      if (placed.section !== 'user') return undefined
      return destination(placed.node.name || placed.node.id, () => { setView('Code'); setCodePalette('Variables'); setCodeMode('manage'); setLeftOpen(true); objectWorkspace.select(placed.node.id) })
    }
    if (['action_id', 'dynamic_action_id'].includes(name) || name === 'id' && rawName.includes('action') && !rawName.includes('static')) {
      const action = devicesWorkspace.actions.find((entry) => entry.actionId === value)
      return action && destination(action.name, () => { setView('Board'); setLeftOpen(true); devicesWorkspace.select({ kind: 'action', id: action.id }) })
    }
    if (['connector_id', 'connector'].includes(name) || name === 'id' && rawName.includes('connector')) {
      const connector = connectorsWorkspace.connectors.find((entry) => entry.id === value)
      return connector && destination(connector.name, () => { setView('Settings'); setSettingsGroup('Connectors'); setLeftOpen(true); connectorsWorkspace.select(connector.key); setRightOpen(true); setDetail('Info') })
    }
    if (name === 'block_idx' || name === 'block') {
      const block = compiledReferences?.blocks.blocks[value]
      return block && destination(block.id, () => openBlock(block.id))
    }
    return undefined
  }

  const activeCanUndo = isCanvas ? canvasWorkspace.canUndo : isCodeVariables ? objectWorkspace.canUndo : isSettingsBle ? bleWorkspace.canUndo : isBoard ? devicesWorkspace.canUndo : false
  const activeCanRedo = isCanvas ? canvasWorkspace.canRedo : isCodeVariables ? objectWorkspace.canRedo : isSettingsBle ? bleWorkspace.canRedo : isBoard ? devicesWorkspace.canRedo : false

  const activeUndo = () => {
    if (isCanvas) canvasWorkspace.undo()
    else if (isCodeVariables) objectWorkspace.undo()
    else if (isSettingsBle) bleWorkspace.undo()
    else if (isBoard) devicesWorkspace.undo()
  }

  const activeRedo = () => {
    if (isCanvas) canvasWorkspace.redo()
    else if (isCodeVariables) objectWorkspace.redo()
    else if (isSettingsBle) bleWorkspace.redo()
    else if (isBoard) devicesWorkspace.redo()
  }

  useEffect(() => {
    if (linkingParentId === undefined) return
    const cancel = (event: KeyboardEvent) => { if (event.key === 'Escape') setLinkingParentId(undefined) }
    window.addEventListener('keydown', cancel)
    return () => window.removeEventListener('keydown', cancel)
  }, [linkingParentId, setLinkingParentId])

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && !event.altKey) {
        if (event.key === 'z' || event.key === 'Z') {
          const target = event.target
          if (target instanceof HTMLElement && (target.isContentEditable || target.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]'))) {
            return
          }
          if (event.shiftKey) {
            if (activeCanRedo) {
              event.preventDefault()
              activeRedo()
            }
          } else {
            if (activeCanUndo) {
              event.preventDefault()
              activeUndo()
            }
          }
        } else if (event.key === 'y' || event.key === 'Y') {
          const target = event.target
          if (target instanceof HTMLElement && (target.isContentEditable || target.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]'))) {
            return
          }
          if (activeCanRedo) {
            event.preventDefault()
            activeRedo()
          }
        }
      }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [activeCanUndo, activeCanRedo, activeUndo, activeRedo])

  useEffect(() => {
    if (view !== 'Code' || codePalette !== 'Variables' || !selectedId || linkingParentId !== undefined) return
    const onDelete = (event: KeyboardEvent) => {
      if (event.key !== 'Delete' || event.repeat || event.altKey || event.ctrlKey || event.metaKey) return
      const target = event.target
      if (target instanceof HTMLElement && (target.isContentEditable || target.closest('input, textarea, select, [contenteditable="true"], [role="textbox"]'))) return
      event.preventDefault()
      remove(selectedId)
    }
    window.addEventListener('keydown', onDelete)
    return () => window.removeEventListener('keydown', onDelete)
  }, [view, codePalette, selectedId, remove, linkingParentId])

  useEffect(() => {
    if (view === 'Settings' && settingsGroup === 'BLE' && bleWorkspace.selectedId === 'general' && detail === 'Info') {
      setRightOpen(false)
    }
  }, [view, settingsGroup, bleWorkspace.selectedId, detail])

  const resizeLeft = (clientX: number) => {
    const minCenterWidth = 420
    const reservedRight = rightOpen ? rightWidth : 54
    const minLeftWidth = 240
    const availableMax = Math.max(minLeftWidth, window.innerWidth - reservedRight - minCenterWidth)
    const maximum = Math.min(500, availableMax)
    const minimum = Math.min(minLeftWidth, maximum)
    setLeftWidth(Math.min(maximum, Math.max(minimum, clientX)))
  }

  const resizeRight = (clientX: number) => {
    const minCenterWidth = 420
    const reservedLeft = leftOpen ? leftWidth : 54
    const availableMax = Math.max(200, window.innerWidth - reservedLeft - minCenterWidth)
    const maximum = Math.min(500, availableMax)
    const minimum = Math.min(260, maximum)
    setRightWidth(Math.min(maximum, Math.max(minimum, window.innerWidth - clientX)))
  }

  const resizeBottom = (clientY: number) => {
    const maximum = Math.max(120, window.innerHeight - 160)
    setTerminalHeight(Math.min(maximum, Math.max(120, window.innerHeight - clientY)))
  }

  const toggleTheme = () => {
    setTheme(theme === 'dark' ? 'light' : 'dark')
  }

  return (
    <DebugHost project={debugProject} sections={objectWorkspace.sections} session={bleConnection.session}>
    <WorkspaceShell className={`theme-${theme} ${resizing ? `is-resizing-${resizing}` : ''} ${linkingParentId !== undefined ? 'is-linking' : ''} ${view === 'Board' && devicesWorkspace.composing ? 'is-composing' : ''}`}>
      {linkingParentId !== undefined && <div className="object-link-backdrop" onClick={() => setLinkingParentId(undefined)} />}
      <div className="workspace-row">
      <ExplorerPanel open={leftOpen} width={leftWidth}>
        {leftOpen && <div className="panel-resize-handle left-resize-handle" role="separator" aria-label="Resize explorer panel" aria-orientation="vertical" tabIndex={0} onPointerDown={(event) => { leftResizeActive.current = true; setResizing('left'); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={(event) => { if (leftResizeActive.current) resizeLeft(event.clientX) }} onPointerUp={() => { leftResizeActive.current = false; setResizing(null) }} onPointerCancel={() => { leftResizeActive.current = false; setResizing(null) }} onKeyDown={(event) => { if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); resizeLeft(leftWidth + (event.key === 'ArrowLeft' ? -16 : 16)) } }} />}
        <div className="side-panel-header left-strip">
          {leftOpen && (
            <div className="view-menu" role="tablist" aria-label="Current view">
              {views.map(({ name, icon: Icon }) => (
                <button
                  key={name}
                  role="tab"
                  aria-selected={view === name}
                  title={name}
                  className={view === name ? 'selected' : ''}
                  onClick={() => {
                    setView(name)
                  }}
                >
                  <Icon aria-hidden="true" />
                </button>
              ))}
            </div>
          )}
          <button className="panel-toggle" aria-label={leftOpen ? 'Collapse explorer' : 'Expand explorer'} onClick={() => setLeftOpen(!leftOpen)}>{leftOpen ? <PanelLeftClose /> : <PanelLeftOpen />}</button>
        </div>
        {leftOpen && <div className="left-content" aria-label={`${view} explorer panel`}>
          {view === 'Home' && <ProjectFilePalette workspace={objectWorkspace} settings={projectSettings} devices={devicesWorkspace.devices} deviceCatalog={devicesWorkspace.catalog} setup={devicesWorkspace.setup} canvases={canvasWorkspace.canvases} board={boardCode} session={bleConnection.session} onNew={newProject} onOpen={(file) => void openProject(file)} onSave={saveProject} onRecover={recoverProject} canRestore={hasPrevious} onRestore={restorePrevious} onSample={loadSample} />}
          {view === 'Settings' && (
            <div className="settings-sidebar-wrapper">
              {settingsGroup === 'all' && (
                <div className="settings-overview-menu">
                  <div className="settings-menu-heading">Settings Categories</div>
                  <button
                    type="button"
                    className="settings-category-btn"
                    onClick={() => setSettingsGroup('BLE')}
                  >
                    <Radio className="settings-cat-icon" aria-hidden="true" />
                    <div className="settings-cat-info">
                      <span className="settings-cat-title">Bluetooth LE</span>
                      <span className="settings-cat-sub">GATT services &amp; characteristics</span>
                    </div>
                    <ChevronRight aria-hidden="true" />
                  </button>
                  <button
                    type="button"
                    className="settings-category-btn"
                    onClick={() => setSettingsGroup('Connectors')}
                  >
                    <Network className="settings-cat-icon" aria-hidden="true" />
                    <div className="settings-cat-info">
                      <span className="settings-cat-title">Data Connectors</span>
                      <span className="settings-cat-sub">Streams, BLE &amp; UART wiring</span>
                    </div>
                    <ChevronRight aria-hidden="true" />
                  </button>
                  <button
                    type="button"
                    className="settings-category-btn"
                    onClick={() => setSettingsGroup('Logging')}
                  >
                    <ScrollText className="settings-cat-icon" aria-hidden="true" />
                    <div className="settings-cat-info">
                      <span className="settings-cat-title">Logging</span>
                      <span className="settings-cat-sub">Log level &amp; serial mirror</span>
                    </div>
                    <ChevronRight aria-hidden="true" />
                  </button>
                  <button
                    type="button"
                    className="settings-category-btn"
                    onClick={() => setSettingsGroup('General')}
                  >
                    <Sliders className="settings-cat-icon" aria-hidden="true" />
                    <div className="settings-cat-info">
                      <span className="settings-cat-title">General</span>
                      <span className="settings-cat-sub">Device identity &amp; RF parameters</span>
                    </div>
                    <ChevronRight aria-hidden="true" />
                  </button>
                </div>
              )}
              {settingsGroup === 'BLE' && (
                <BleSettingsPalette
                  workspace={bleWorkspace}
                  onBackToSettings={() => setSettingsGroup('all')}
                />
              )}
              {settingsGroup === 'Connectors' && (
                <DataConnectorsPalette
                  workspace={connectorsWorkspace}
                  onBackToSettings={() => setSettingsGroup('all')}
                />
              )}
              {settingsGroup === 'Logging' && (
                <div className="settings-general-sidebar">
                  <div className="ble-palette-back-bar">
                    <button type="button" className="settings-back-btn" onClick={() => setSettingsGroup('all')} title="Back to all settings">
                      <ArrowLeft aria-hidden="true" />
                      <span>All Settings</span>
                    </button>
                  </div>
                  <div className="settings-overview-menu">
                    <div className="settings-menu-heading">Logging</div>
                  </div>
                </div>
              )}
              {settingsGroup === 'General' && (
                <div className="settings-general-sidebar">
                  <div className="ble-palette-back-bar">
                    <button
                      type="button"
                      className="settings-back-btn"
                      onClick={() => setSettingsGroup('all')}
                      title="Back to all settings"
                    >
                      <ArrowLeft aria-hidden="true" />
                      <span>All Settings</span>
                    </button>
                  </div>
                  <div className="settings-overview-menu">
                    <div className="settings-menu-heading">General Settings</div>
                  </div>
                </div>
              )}
            </div>
          )}
          {view === 'Code' && <div className="code-palette">
            <div className="code-palette-tabs" role="tablist" aria-label="Code palettes">
              {codePalettes.map((palette) => <button key={palette} role="tab" aria-selected={codePalette === palette} className={codePalette === palette ? 'selected' : ''} onClick={() => {
                // Both palettes keep whichever view is open: on the canvas Blocks are placed and Variables dragged onto blocks; in View / Manage
                // Blocks are tiles that open their description and Variables the variable editor.
                setCodePalette(palette)
              }}>{palette}</button>)}
            </div>
            <div className="code-palette-body" aria-label={`${codePalette} palette`}>{codePalette === 'Variables' && <ObjectTreePalette workspace={objectWorkspace} />}{codePalette === 'Blocks' && <BlockPalette workspace={canvasWorkspace} tiles={codeMode === 'manage' ? { selectedKey: manageBlockKey, onSelect: (key: string) => { setManageBlockKey(key); setRightOpen(true); setDetail('Info') } } : undefined} />}</div>
            <div className="code-mode-footer">
              <button className="code-mode-toggle" onClick={() => {
                setCodeMode(codeMode === 'manage' ? 'canvas' : 'manage')
              }}><ArrowLeftRight aria-hidden="true" /><span>Switch to {codeMode === 'manage' ? 'Canvas' : 'View / Manage'}</span></button>
            </div>
          </div>}
          {view === 'Board' && <DevicesPalette workspace={devicesWorkspace} boardMode={boardMode} onToggleMode={() => setBoardMode(boardMode === 'manage' ? 'canvas' : 'manage')} />}
          {view === 'Remote' && <div className="remote-palette" aria-label="Gamepad controls palette">
            <div className="remote-palette-title">Controls</div>
            <div className="remote-palette-body" />
          </div>}
        </div>}
        {!leftOpen && (
          <div className="side-panel-collapsed-icons left-collapsed-icons">
            <div className="view-menu" role="tablist" aria-label="Current view">
              {views.map(({ name, icon: Icon }) => (
                <button
                  key={name}
                  role="tab"
                  aria-selected={view === name}
                  title={name}
                  className={view === name ? 'selected' : ''}
                  onClick={() => {
                    setView(name)
                    setLeftOpen(true)
                  }}
                >
                  <Icon aria-hidden="true" />
                </button>
              ))}
            </div>
          </div>
        )}
      </ExplorerPanel>

      <div className="center-column">
      <MainScreen label={view === 'Settings' ? `${settingsGroup} screen` : view === 'Code' ? `${codePalette} ${codeMode} screen` : `${view} main panel`} toolbar={<>
          <div className="history-actions">
            <button aria-label="Undo" title="Undo (Ctrl+Z)" disabled={!activeCanUndo} onClick={activeUndo}><Undo2 aria-hidden="true" /></button>
            <button aria-label="Redo" title="Redo (Ctrl+Y)" disabled={!activeCanRedo} onClick={activeRedo}><Redo2 aria-hidden="true" /></button>
          </div>
          {view === 'Settings' && (settingsGroup === 'BLE' || settingsGroup === 'General' || settingsGroup === 'Connectors') && (
            <>
              <button
                className={`ble-apply-button ${bleConnection.isConnected ? 'is-connected' : 'is-disconnected'} ${settingsDirty ? 'has-changes' : ''} ${bleWorkspace.justApplied ? 'applied' : ''}`}
                disabled={settingsSync.busy}
                onClick={() => void applySettings()}
                title={
                  !bleConnection.isConnected
                    ? 'Device not connected — click to connect BLE device'
                    : !settingsSync.plan.ok
                    ? `Can't apply: ${settingsSync.plan.diagnostics.find((entry) => entry.severity === 'error')?.message ?? 'the settings have errors'}`
                    : settingsSync.pending
                    ? `Apply ${settingsSync.pending} change(s) to the connected board`
                    : 'The board matches these settings'
                }
                aria-label={
                  !bleConnection.isConnected
                    ? 'Device not connected'
                    : 'Apply configuration changes'
                }
              >
                <Check aria-hidden="true" />
              </button>
              <button
                className={`ble-connect-bar-btn ${bleConnection.isConnected ? 'connected' : ''}`}
                onClick={() => {
                  if (!bleConnection.isConnected) {
                    void bleConnection.connect()
                  } else {
                    setRightOpen(true)
                    setDetail('Connect')
                  }
                }}
                title={
                  bleConnection.isConnected
                    ? `Connected to ${bleConnection.device?.name || 'BLE device'} (Click for details)`
                    : 'Connect BLE device'
                }
                aria-label="Connect BLE device"
              >
                <Plug aria-hidden="true" />
                <span>{bleConnection.isConnected ? (bleConnection.device?.name || 'Connected') : 'Connect'}</span>
              </button>
            </>
          )}
          <div className="screen-actions" aria-label={`${view} specific actions`}>
            {isCanvas && <button aria-label="Detailed block view" title="Toggle detailed block view" aria-pressed={canvasWorkspace.detailed} className={canvasWorkspace.detailed ? 'selected' : ''} onClick={() => canvasWorkspace.setDetailed(!canvasWorkspace.detailed)}><ListTree aria-hidden="true" /></button>}
            {isCanvas && <button aria-label={showCanvasGrid ? 'Hide canvas grid' : 'Show canvas grid'} title={showCanvasGrid ? 'Hide canvas grid' : 'Show canvas grid'} aria-pressed={showCanvasGrid} className={showCanvasGrid ? 'selected' : ''} onClick={() => setShowCanvasGrid(!showCanvasGrid)}><Grid2X2 aria-hidden="true" /></button>}
            {isCanvas && <button aria-label="Select multiple blocks" title="Select multiple: tap blocks to add or remove them, drag the background to draw a box (Shift or Ctrl also add)" aria-pressed={canvasWorkspace.multiSelect} className={canvasWorkspace.multiSelect ? 'selected' : ''} onClick={() => canvasWorkspace.setMultiSelect(!canvasWorkspace.multiSelect)}><BoxSelect aria-hidden="true" /></button>}
            {isCanvas && <button aria-label="Delete selection" title="Delete the selected wire, variable or blocks (same as Delete)" disabled={!canvasWorkspace.deleteSelection} onClick={() => canvasWorkspace.deleteSelection?.()}><Trash2 aria-hidden="true" /></button>}
            {isCanvas && <button aria-label="Copy block" title="Copy the selected blocks (Ctrl+C)" disabled={canvasWorkspace.selectedIds.length === 0} onClick={canvasWorkspace.copy}><Copy aria-hidden="true" /></button>}
            {isCanvas && <button aria-label="Paste block" title="Paste the copied blocks (Ctrl+V)" disabled={!canvasWorkspace.canPaste} onClick={canvasWorkspace.paste}><ClipboardPaste aria-hidden="true" /></button>}
            {isCanvas && (
              <button
                aria-label="Empty variable: drag onto a block pin"
                title="Empty variable: drag onto a block pin, then choose it in the block details"
                draggable
                className="canvas-empty-variable"
                onDragStart={(event) => {
                  event.dataTransfer.setData(OBJECT_DRAG_TYPE, '')
                  event.dataTransfer.setData(objectKindDragType('any'), '')
                  event.dataTransfer.effectAllowed = 'all'
                }}
              ><Variable aria-hidden="true" /></button>
            )}
            {isCanvas && <button aria-label={canvasWorkspace.snap ? 'Turn snap to grid off' : 'Turn snap to grid on'} title={canvasWorkspace.snap ? 'Snap to grid: on' : 'Snap to grid: off'} aria-pressed={canvasWorkspace.snap} className={canvasWorkspace.snap ? 'selected' : ''} onClick={() => canvasWorkspace.setSnap(!canvasWorkspace.snap)}><Magnet aria-hidden="true" /></button>}
          </div>
          <button className="theme-toggle" aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} mode`} title={`Switch to ${theme === 'dark' ? 'light' : 'dark'} mode`} onClick={toggleTheme}>{theme === 'dark' ? <Sun /> : <Moon />}</button>
      </>}>
          {isCanvas && (
            <CanvasEditor
              workspace={canvasWorkspace}
              showGrid={showCanvasGrid}
              diagnostics={canvasDiagnostics}
              devices={devicesWorkspace.devices}
              deviceCatalog={devicesWorkspace.catalog}
              project={objectWorkspace.project}
              selectedObjectId={codePalette === 'Variables' ? (objectWorkspace.selectedId ?? undefined) : undefined}
              onClearSelectedObject={() => objectWorkspace.select(null)}
            />
          )}
          {view === 'Code' && codePalette === 'Variables' && codeMode === 'manage' && <ObjectTreeEditor workspace={objectWorkspace} />}
          {view === 'Code' && codePalette === 'Blocks' && codeMode === 'manage' && <BlockManager typeKey={manageBlockKey} />}
          {view === 'Home' && <ProjectFilePage workspace={objectWorkspace} settings={projectSettings} devices={devicesWorkspace.devices} deviceCatalog={devicesWorkspace.catalog} setup={devicesWorkspace.setup} canvases={canvasWorkspace.canvases} onRecover={recoverProject} />}
          {view === 'Board' && boardMode === 'canvas' && <BoardCanvas />}
          {view === 'Board' && boardMode === 'manage' && <DevicesEditor workspace={devicesWorkspace} session={bleConnection.session} canvases={canvasWorkspace.canvases} onOpenBlock={openBlockAt} pinDestination={pinDestination} onPinDestinationHandled={() => setPinDestination(undefined)} />}
          {view === 'Settings' && settingsGroup === 'BLE' && <BleSettingsEditor workspace={bleWorkspace} />}
          {view === 'Settings' && settingsGroup === 'all' && (
            <div className="object-editor settings-overview-editor">
              <div className="settings-overview-header">
                <Settings2 aria-hidden="true" />
                <h1>Settings</h1>
              </div>
              <div className="settings-overview-grid">
                <div
                  className="settings-overview-card"
                  onClick={() => setSettingsGroup('BLE')}
                  role="button"
                  tabIndex={0}
                >
                  <Radio className="settings-overview-icon" aria-hidden="true" />
                  <h2>Bluetooth LE (BLE)</h2>
                  <p>Configure GATT profile services, characteristics, UUIDs, broadcast advertising, and connector streams.</p>
                  <span className="settings-card-action">
                    Open BLE Settings <ChevronRight aria-hidden="true" />
                  </span>
                </div>
                <div
                  className="settings-overview-card"
                  onClick={() => setSettingsGroup('Connectors')}
                  role="button"
                  tabIndex={0}
                >
                  <Network className="settings-overview-icon" aria-hidden="true" />
                  <h2>Data Connectors</h2>
                  <p>Configure transport-agnostic logical streams (telemetry, commands, logs, errors) and wire them to BLE or UART providers as in runit.c.</p>
                  <span className="settings-card-action">
                    Open Data Connectors <ChevronRight aria-hidden="true" />
                  </span>
                </div>
                <div
                  className="settings-overview-card"
                  onClick={() => setSettingsGroup('Logging')}
                  role="button"
                  tabIndex={0}
                >
                  <ScrollText className="settings-overview-icon" aria-hidden="true" />
                  <h2>Logging</h2>
                  <p>Configure the board's log level, serial mirroring and error-chain tracing.</p>
                  <span className="settings-card-action">
                    Open Logging <ChevronRight aria-hidden="true" />
                  </span>
                </div>
                <div
                  className="settings-overview-card"
                  onClick={() => setSettingsGroup('General')}
                  role="button"
                  tabIndex={0}
                >
                  <Sliders className="settings-overview-icon" aria-hidden="true" />
                  <h2>General Settings</h2>
                  <p>Configure device broadcast identity, RF transmission parameters, MTU sizing, and pairing passkey.</p>
                  <span className="settings-card-action">
                    Open General Settings <ChevronRight aria-hidden="true" />
                  </span>
                </div>
              </div>
            </div>
          )}
          {view === 'Settings' && settingsGroup === 'Connectors' && (
            <DataConnectorsEditor
              workspace={connectorsWorkspace}
              bleProfile={bleWorkspace.profile}
            />
          )}
          {view === 'Settings' && settingsGroup === 'Logging' && <LogSettingsEditor workspace={logWorkspace} />}
          {view === 'Settings' && settingsGroup === 'General' && (
            <div className="object-editor">
              <div className="ble-general-header">
                <Sliders aria-hidden="true" />
                <h1>General Settings</h1>
              </div>
              <p>System identity, board configuration, and power defaults.</p>
              <DebugSettings />
            </div>
          )}
      </MainScreen>
      </div>

      <DetailsPanel
        open={rightOpen}
        expanded={rightExpanded}
        style={rightOpen ? { width: rightExpanded ? Math.max(rightWidth, 500) : rightWidth } : undefined}
      >
        {rightOpen && <div className="panel-resize-handle resize-handle" role="separator" aria-label="Resize details panel" aria-orientation="vertical" tabIndex={0} onPointerDown={(event) => { resizeActive.current = true; setResizing('right'); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={(event) => { if (resizeActive.current) resizeRight(event.clientX) }} onPointerUp={() => { resizeActive.current = false; setResizing(null) }} onPointerCancel={() => { resizeActive.current = false; setResizing(null) }} onKeyDown={(event) => { if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); resizeRight(window.innerWidth - rightWidth + (event.key === 'ArrowLeft' ? -16 : 16)) } }} />}
        <div className="side-panel-header right-header">
          {rightOpen && (
            <div className="detail-menu" role="tablist" aria-label="Detail sections">
              {details.map(({ name, icon: Icon }) => (
                <button
                  key={name}
                  role="tab"
                  aria-selected={detail === name}
                  title={name === 'Connect' && bleConnection.isConnected ? `Connected: ${bleConnection.device?.name || 'BLE Device'}` : name}
                  className={`${detail === name ? 'selected' : ''} ${name === 'Connect' && bleConnection.isConnected ? 'is-connected' : ''}`}
                  onClick={() => {
                    setDetail(name)
                  }}
                >
                  <Icon aria-hidden="true" />
                </button>
              ))}
            </div>
          )}
          {rightOpen && <button className={`terminal-launch ${terminalOpen ? 'selected' : ''}`} aria-label={terminalOpen ? 'Hide terminal' : 'Show terminal'} title="Terminal" aria-pressed={terminalOpen} onClick={() => setTerminalOpen(!terminalOpen)}><SquareTerminal aria-hidden="true" /></button>}
          <button className="panel-toggle detail-toggle" aria-label={rightOpen ? 'Collapse details' : 'Expand details'} onClick={() => setRightOpen(!rightOpen)}>{rightOpen ? <PanelRightClose /> : <PanelRightOpen />}</button>
        </div>
        {rightOpen && (
          <div className="detail-content" aria-label={`${detail} content panel`}>
            {view === 'Board' && boardMode === 'manage' && detail === 'Info' && <DeviceDetails workspace={devicesWorkspace} session={bleConnection.session} canvases={canvasWorkspace.canvases} onOpenBlock={openBlockAt} />}
              {isCanvas && detail === 'Info' && (
              <BlockDetails
                workspace={canvasWorkspace}
                diagnostics={canvasDiagnostics}
                devices={devicesWorkspace.devices}
                deviceCatalog={devicesWorkspace.catalog}
                project={objectWorkspace.project}
                selectedObjectId={objectWorkspace.selectedId ?? undefined}
                onClearSelectedObject={() => objectWorkspace.select(undefined)}
                onOpenDevice={openDevice}
                onOpenPin={openDevicePin}
                onCreateVariable={(node) => {
                  objectWorkspace.edit((proj) => addObject(proj, null, node))
                  objectWorkspace.select(node.id)
                }}
                onExpandDetails={(expanded) => {
                  setRightExpanded(expanded)
                  if (expanded) {
                    if (rightWidth < 500) {
                      prevRightWidthRef.current = rightWidth
                      setRightWidth(500)
                    }
                  } else {
                    if (prevRightWidthRef.current !== undefined) {
                      setRightWidth(prevRightWidthRef.current)
                      prevRightWidthRef.current = undefined
                    }
                  }
                }}
              />
            )}
            {view === 'Code' && codePalette === 'Blocks' && !isCanvas && detail === 'Info' && (
              <BlockUses typeKey={manageBlockKey} canvases={canvasWorkspace.canvases} onOpenBlock={openBlockAt} />
            )}
            {view === 'Code' && codePalette === 'Variables' && !isCanvas && detail === 'Info' && (
              <ObjectDetails workspace={objectWorkspace} onJump={(id) => { setLeftOpen(true); objectWorkspace.select(id) }} canvases={canvasWorkspace.canvases} onOpenBlock={(canvasId, blockId) => { setView('Code'); setCodeMode('canvas'); canvasWorkspace.reveal(canvasId, blockId) }} />
            )}
            {view === 'Settings' && settingsGroup === 'BLE' && detail === 'Info' && (
              <BleDetails workspace={bleWorkspace} onJump={(id) => { setLeftOpen(true); bleWorkspace.select(id) }} />
            )}
            {view === 'Settings' && settingsGroup === 'Connectors' && detail === 'Info' && (
              <DataConnectorDetails
                workspace={connectorsWorkspace}
                onJumpToBle={() => setSettingsGroup('BLE')}
              />
            )}
            {detail === 'Connect' && (
              <BleConnectPanel
                connection={bleConnection}
                onApplyProfile={() => void applySettings()}
                hasUnappliedChanges={settingsDirty}
                pendingCommands={settingsSync.pending}
                applyBusy={settingsSync.busy}
                applyMessage={settingsSync.message}
                applyReading={settingsSync.reading}
              />
            )}
            {detail === 'Debug' && <DebugPanel project={debugProject} />}
            {detail === 'Run' && <ProgramPanel workspace={objectWorkspace} connection={bleConnection} board={boardCode} settings={projectSettings} devices={devicesWorkspace.devices} deviceCatalog={devicesWorkspace.catalog} setup={devicesWorkspace.setup} canvases={canvasWorkspace.canvases} />}
          </div>
        )}
        {!rightOpen && (
          <div className="side-panel-collapsed-icons right-collapsed-icons">
            {details.map(({ name, icon: Icon }) => (
              <button
                key={name}
                role="tab"
                aria-selected={detail === name}
                title={name === 'Connect' && bleConnection.isConnected ? `Connected: ${bleConnection.device?.name || 'BLE Device'}` : name}
                className={`${detail === name ? 'selected' : ''} ${name === 'Connect' && bleConnection.isConnected ? 'is-connected' : ''}`}
                onClick={() => {
                  setDetail(name)
                  setRightOpen(true)
                }}
              >
                <Icon aria-hidden="true" />
              </button>
            ))}
            <button className={`terminal-launch ${terminalOpen ? 'selected' : ''}`} aria-label={terminalOpen ? 'Hide terminal' : 'Show terminal'} title="Terminal" aria-pressed={terminalOpen} onClick={() => setTerminalOpen(!terminalOpen)}><SquareTerminal aria-hidden="true" /></button>
          </div>
        )}
      </DetailsPanel>
      </div>
      {terminalOpen && <section className="terminal-panel" style={{ height: terminalHeight }} aria-label="Terminal panel">
        <div className="panel-resize-handle bottom-resize-handle" role="separator" aria-label="Resize terminal panel" aria-orientation="horizontal" tabIndex={0} onPointerDown={(event) => { bottomResizeActive.current = true; setResizing('bottom'); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={(event) => { if (bottomResizeActive.current) resizeBottom(event.clientY) }} onPointerUp={() => { bottomResizeActive.current = false; setResizing(null) }} onPointerCancel={() => { bottomResizeActive.current = false; setResizing(null) }} onKeyDown={(event) => { if (event.key === 'ArrowUp' || event.key === 'ArrowDown') { event.preventDefault(); resizeBottom(window.innerHeight - terminalHeight + (event.key === 'ArrowUp' ? -16 : 16)) } }} />
        <div className="terminal-header">
          <div className="terminal-tabs" role="tablist" aria-label="Terminal views">
            {(['Commands', 'Errors & logs'] as const).map((tab) => <button key={tab} role="tab" aria-selected={terminalTab === tab} className={terminalTab === tab ? 'selected' : ''} onClick={() => setTerminalTab(tab)}>{tab}</button>)}
          </div>
          <button aria-label="Close terminal" title="Close terminal" onClick={() => setTerminalOpen(false)}><X aria-hidden="true" /></button>
        </div>
        <div className="terminal-body">
          {terminalTab === 'Commands' ? <CommandConsole session={bleConnection.session} resolveReference={resolveConsoleReference} /> : <DiagnosticsConsole session={bleConnection.session} resolveReference={resolveConsoleReference} />}
        </div>
      </section>}
    </WorkspaceShell>
    </DebugHost>
  )
}
