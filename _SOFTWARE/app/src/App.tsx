import { useState, useRef, useEffect } from 'react'
import {
  Settings,
  Cpu,
  Boxes,
  Code2,
  Gamepad2,
  PanelLeftClose,
  PanelLeftOpen,
  PanelRightClose,
  PanelRightOpen,
  Plug,
  Play,
  Square,
  Eye,
  SlidersHorizontal,
  ChevronRight,
  ChevronDown,
  Radio,
  ShieldAlert,
  Palette,
  HardDrive,
  Check,
  X,
  Plus,
  CircuitBoard,
  Layers,
  ZoomIn,
  ZoomOut,
  Crosshair
} from 'lucide-react'

export type LeftNavTab = 'settings' | 'board' | 'devices' | 'code' | 'remote'
export type SettingsSubTab = 'connectivity' | 'error_handling' | 'appearance' | 'system'
export type RightActionTab = 'connect' | 'run' | 'stop' | 'watch' | 'settings'

const DEFAULT_RIGHT_WIDTH = 280
const EXTENDED_RIGHT_WIDTH = 380

// Wireframe Mock Device Items
const ONBOARD_DEVICES = [
  { id: 'dev-pca9685', name: 'PCA9685 PWM Expander', chip: 'PCA9685', addr: '0x40', bus: 'I2C-1' },
  { id: 'dev-tps55289', name: 'TPS55289 Buck-Boost', chip: 'TPS55289', addr: '0x74', bus: 'I2C-1' },
  { id: 'dev-tca6424a', name: 'TCA6424A IO Expander', chip: 'TCA6424A', addr: '0x22', bus: 'I2C-1' },
  { id: 'dev-ina226', name: 'INA226 Power Monitor', chip: 'INA226', addr: '0x45', bus: 'I2C-1' },
]

const INSTALLED_DEVICES = [
  { id: 'dev-bno085', name: 'BNO085 9-DOF IMU', chip: 'BNO085', addr: '0x4A', bus: 'I2C-0' },
  { id: 'dev-vl53l1x', name: 'VL53L1X ToF Distance', chip: 'VL53L1X', addr: '0x29', bus: 'I2C-0' },
]

export default function App() {
  // Navigation State
  const [leftTab, setLeftTab] = useState<LeftNavTab>('devices')
  const [settingsSubTab, setSettingsSubTab] = useState<SettingsSubTab>('connectivity')
  const [rightTab, setRightTab] = useState<RightActionTab>('settings')

  // Selected config element inside main content (settings view)
  const [selectedConfigElement, setSelectedConfigElement] = useState<number | null>(null)

  // Device section states
  const [onboardCollapsed, setOnboardCollapsed] = useState(true) // onboard starts collapsed
  const [installedCollapsed, setInstalledCollapsed] = useState(false)
  const [selectedDeviceId, setSelectedDeviceId] = useState<string | null>(null)

  // Layout Visibility States
  const [leftSidebarOpen, setLeftSidebarOpen] = useState(true)
  const [rightPanelOpen, setRightPanelOpen] = useState(true)

  // Layout Dimensions (Resizable)
  const [leftWidth, setLeftWidth] = useState(280)
  const [rightWidth, setRightWidth] = useState(DEFAULT_RIGHT_WIDTH)

  // Dragging / Resizing references
  const isDraggingLeft = useRef(false)
  const isDraggingRight = useRef(false)

  // SVG Pan & Zoom state for interactive board visualizer
  const [zoomLevel, setZoomLevel] = useState(1)

  useEffect(() => {
    const handleMouseMove = (e: MouseEvent) => {
      if (isDraggingLeft.current) {
        const newWidth = Math.min(Math.max(180, e.clientX), 550)
        setLeftWidth(newWidth)
      } else if (isDraggingRight.current) {
        const newWidth = Math.min(Math.max(180, window.innerWidth - e.clientX), 700)
        setRightWidth(newWidth)
      }
    }

    const handleMouseUp = () => {
      isDraggingLeft.current = false
      isDraggingRight.current = false
      document.body.style.cursor = 'default'
    }

    window.addEventListener('mousemove', handleMouseMove)
    window.addEventListener('mouseup', handleMouseUp)
    return () => {
      window.removeEventListener('mousemove', handleMouseMove)
      window.removeEventListener('mouseup', handleMouseUp)
    }
  }, [])

  // When an element in settings config is clicked
  const handleConfigElementClick = (idx: number, e: React.MouseEvent) => {
    e.stopPropagation()
    setSelectedConfigElement(idx)
    setRightPanelOpen(true)
    setRightTab('settings')
    setRightWidth(prev => (prev < EXTENDED_RIGHT_WIDTH ? EXTENDED_RIGHT_WIDTH : prev))
  }

  // When a device in the list or interactive SVG is clicked:
  // opens right panel, switches to settings view for this device, and joins inspection
  const handleDeviceClick = (deviceId: string, e?: React.MouseEvent) => {
    if (e) e.stopPropagation()
    setSelectedDeviceId(deviceId)
    setRightPanelOpen(true)
    setRightTab('settings')
    setRightWidth(prev => (prev < EXTENDED_RIGHT_WIDTH ? EXTENDED_RIGHT_WIDTH : prev))
  }

  // Returns to normal unextended state
  const handleResetSelection = () => {
    if (selectedConfigElement !== null || selectedDeviceId !== null) {
      setSelectedConfigElement(null)
      setSelectedDeviceId(null)
      setRightWidth(DEFAULT_RIGHT_WIDTH)
    }
  }

  // Is Right Panel currently joined/docked beside Left panel?
  // When in device view and a device is selected, the layout docks right panel next to left panel
  const isRightJoinedWithLeft = leftTab === 'devices' && selectedDeviceId !== null

  return (
    <div className="flex flex-col h-screen w-screen bg-neutral-950 text-neutral-200 overflow-hidden font-mono text-xs select-none">

      {/* ========================================================= */}
      {/* MAIN CONTAINER                                            */}
      {/* If isRightJoinedWithLeft is true:                         */}
      {/* [LEFT PANEL] + [RIGHT PANEL DOCKED] + [CENTER CANVAS SVG] */}
      {/* Otherwise standard:                                       */}
      {/* [LEFT PANEL] + [CENTER CANVAS] + [RIGHT PANEL]            */}
      {/* ========================================================= */}
      <div className="flex flex-1 min-h-0 relative overflow-hidden">

        {/* ======================================================= */}
        {/* 1. LEFT COLUMN: CONSOLIDATED TOP BAR + BODY             */}
        {/* ======================================================= */}
        {leftSidebarOpen ? (
          <aside
            style={{ width: `${leftWidth}px` }}
            className="h-full bg-neutral-900 border-r border-neutral-800 flex flex-col shrink-0 select-none relative group transition-[width] duration-150"
          >
            {/* CONSOLIDATED LEFT TOP BAR */}
            <div className="h-11 border-b border-neutral-800 bg-neutral-900/95 px-2 flex items-center justify-between shrink-0">
              <div className="flex items-center gap-1">
                <button
                  title="Settings"
                  onClick={() => {
                    setLeftTab('settings')
                    setRightTab('settings')
                    handleResetSelection()
                  }}
                  className={`p-1.5 rounded transition-colors ${
                    leftTab === 'settings'
                      ? 'bg-neutral-800 text-neutral-100 shadow-sm'
                      : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                  }`}
                >
                  <Settings size={16} />
                </button>

                <div className="h-4 w-px bg-neutral-800 mx-0.5" />

                <button
                  title="Board"
                  onClick={() => {
                    setLeftTab('board')
                    handleResetSelection()
                  }}
                  className={`p-1.5 rounded transition-colors ${
                    leftTab === 'board'
                      ? 'bg-neutral-800 text-neutral-100 shadow-sm'
                      : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                  }`}
                >
                  <Cpu size={16} />
                </button>

                <button
                  title="Devices"
                  onClick={() => {
                    setLeftTab('devices')
                    handleResetSelection()
                  }}
                  className={`p-1.5 rounded transition-colors ${
                    leftTab === 'devices'
                      ? 'bg-neutral-800 text-neutral-100 shadow-sm'
                      : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                  }`}
                >
                  <Boxes size={16} />
                </button>

                <button
                  title="Code"
                  onClick={() => {
                    setLeftTab('code')
                    handleResetSelection()
                  }}
                  className={`p-1.5 rounded transition-colors ${
                    leftTab === 'code'
                      ? 'bg-neutral-800 text-neutral-100 shadow-sm'
                      : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                  }`}
                >
                  <Code2 size={16} />
                </button>

                <button
                  title="Remote"
                  onClick={() => {
                    setLeftTab('remote')
                    handleResetSelection()
                  }}
                  className={`p-1.5 rounded transition-colors ${
                    leftTab === 'remote'
                      ? 'bg-neutral-800 text-neutral-100 shadow-sm'
                      : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                  }`}
                >
                  <Gamepad2 size={16} />
                </button>
              </div>

              {/* Collapse Left Sidebar */}
              <button
                onClick={() => setLeftSidebarOpen(false)}
                className="p-1.5 rounded text-neutral-500 hover:text-neutral-200 hover:bg-neutral-800 transition-colors"
                title="Collapse Sidebar"
              >
                <PanelLeftClose size={15} />
              </button>
            </div>

            {/* Left Sidebar Body */}
            <div className="flex-1 p-2 space-y-2 overflow-y-auto">
              {leftTab === 'devices' ? (
                /* DEVICES SECTION: LIST (BOARD ONBOARD + INSTALLED) */
                <div className="flex flex-col gap-2">

                  {/* Action Header Slot: Add Device */}
                  <div className="flex items-center justify-between pb-1.5 border-b border-neutral-800/80">
                    <span className="text-[10px] text-neutral-500 uppercase tracking-wider font-semibold">
                      DEVICES_LIST
                    </span>
                    <button
                      className="p-1 rounded bg-neutral-800 hover:bg-neutral-700 text-neutral-300 transition-colors flex items-center gap-1 text-[10px]"
                      title="Add Device"
                    >
                      <Plus size={12} />
                      <span>ADD</span>
                    </button>
                  </div>

                  {/* SUBSECTION 1: ONBOARD HARDWARE (STARTS COLLAPSED) */}
                  <div className="border border-neutral-800/80 rounded bg-neutral-950/40 overflow-hidden">
                    <button
                      onClick={() => setOnboardCollapsed(!onboardCollapsed)}
                      className="w-full flex items-center justify-between p-2 text-left bg-neutral-900/60 hover:bg-neutral-900 transition-colors text-neutral-300 font-medium text-[11px]"
                    >
                      <span className="flex items-center gap-1.5">
                        <CircuitBoard size={14} className="text-sky-400" />
                        <span>[ONBOARD_DEVICES]</span>
                      </span>
                      <span className="flex items-center gap-1 text-neutral-500 text-[10px]">
                        <span>({ONBOARD_DEVICES.length})</span>
                        {onboardCollapsed ? <ChevronRight size={13} /> : <ChevronDown size={13} />}
                      </span>
                    </button>

                    {!onboardCollapsed && (
                      <div className="p-1.5 space-y-1 border-t border-neutral-800/70 bg-neutral-950/20">
                        {ONBOARD_DEVICES.map(dev => (
                          <div
                            key={dev.id}
                            onClick={(e) => handleDeviceClick(dev.id, e)}
                            className={`p-2 rounded border cursor-pointer transition-all flex items-center justify-between ${
                              selectedDeviceId === dev.id
                                ? 'border-sky-500 bg-sky-950/30 text-neutral-100 shadow-sm'
                                : 'border-neutral-850 bg-neutral-900/40 text-neutral-400 hover:border-neutral-700 hover:text-neutral-200'
                            }`}
                          >
                            <div className="flex flex-col">
                              <span className="font-semibold text-[11px]">{dev.name}</span>
                              <span className="text-[9px] text-neutral-500 font-mono">
                                {dev.chip} | {dev.addr} ({dev.bus})
                              </span>
                            </div>
                            <ChevronRight
                              size={13}
                              className={selectedDeviceId === dev.id ? 'text-sky-400' : 'text-neutral-600'}
                            />
                          </div>
                        ))}
                      </div>
                    )}
                  </div>

                  {/* SUBSECTION 2: INSTALLED / USER PERIPHERALS */}
                  <div className="border border-neutral-800/80 rounded bg-neutral-950/40 overflow-hidden">
                    <button
                      onClick={() => setInstalledCollapsed(!installedCollapsed)}
                      className="w-full flex items-center justify-between p-2 text-left bg-neutral-900/60 hover:bg-neutral-900 transition-colors text-neutral-300 font-medium text-[11px]"
                    >
                      <span className="flex items-center gap-1.5">
                        <Layers size={14} className="text-emerald-400" />
                        <span>[INSTALLED_PERIPHERALS]</span>
                      </span>
                      <span className="flex items-center gap-1 text-neutral-500 text-[10px]">
                        <span>({INSTALLED_DEVICES.length})</span>
                        {installedCollapsed ? <ChevronRight size={13} /> : <ChevronDown size={13} />}
                      </span>
                    </button>

                    {!installedCollapsed && (
                      <div className="p-1.5 space-y-1 border-t border-neutral-800/70 bg-neutral-950/20">
                        {INSTALLED_DEVICES.map(dev => (
                          <div
                            key={dev.id}
                            onClick={(e) => handleDeviceClick(dev.id, e)}
                            className={`p-2 rounded border cursor-pointer transition-all flex items-center justify-between ${
                              selectedDeviceId === dev.id
                                ? 'border-sky-500 bg-sky-950/30 text-neutral-100 shadow-sm'
                                : 'border-neutral-850 bg-neutral-900/40 text-neutral-400 hover:border-neutral-700 hover:text-neutral-200'
                            }`}
                          >
                            <div className="flex flex-col">
                              <span className="font-semibold text-[11px]">{dev.name}</span>
                              <span className="text-[9px] text-neutral-500 font-mono">
                                {dev.chip} | {dev.addr} ({dev.bus})
                              </span>
                            </div>
                            <ChevronRight
                              size={13}
                              className={selectedDeviceId === dev.id ? 'text-sky-400' : 'text-neutral-600'}
                            />
                          </div>
                        ))}
                      </div>
                    )}
                  </div>

                </div>
              ) : leftTab === 'settings' ? (
                /* Pure Wireframe Settings Subtabs */
                <div className="flex flex-col gap-2">
                  <button
                    onClick={() => {
                      setSettingsSubTab('connectivity')
                      handleResetSelection()
                    }}
                    className={`w-full flex items-center justify-between p-2 rounded border text-left transition-colors ${
                      settingsSubTab === 'connectivity'
                        ? 'bg-neutral-800/90 border-neutral-600 text-neutral-100'
                        : 'bg-neutral-950/40 border-neutral-800/80 text-neutral-400 hover:bg-neutral-850 hover:text-neutral-200'
                    }`}
                  >
                    <div className="flex items-center gap-2">
                      <Radio size={14} className={settingsSubTab === 'connectivity' ? 'text-sky-400' : 'text-neutral-500'} />
                      <span className="font-medium text-[11px]">[SUBTAB_CONNECTIVITY]</span>
                    </div>
                    <ChevronRight size={12} className="text-neutral-600" />
                  </button>

                  <button
                    onClick={() => {
                      setSettingsSubTab('error_handling')
                      handleResetSelection()
                    }}
                    className={`w-full flex items-center justify-between p-2 rounded border text-left transition-colors ${
                      settingsSubTab === 'error_handling'
                        ? 'bg-neutral-800/90 border-neutral-600 text-neutral-100'
                        : 'bg-neutral-950/40 border-neutral-800/80 text-neutral-400 hover:bg-neutral-850 hover:text-neutral-200'
                    }`}
                  >
                    <div className="flex items-center gap-2">
                      <ShieldAlert size={14} className={settingsSubTab === 'error_handling' ? 'text-amber-400' : 'text-neutral-500'} />
                      <span className="font-medium text-[11px]">[SUBTAB_ERROR_HANDLING]</span>
                    </div>
                    <ChevronRight size={12} className="text-neutral-600" />
                  </button>

                  <button
                    onClick={() => {
                      setSettingsSubTab('appearance')
                      handleResetSelection()
                    }}
                    className={`w-full flex items-center justify-between p-2 rounded border text-left transition-colors ${
                      settingsSubTab === 'appearance'
                        ? 'bg-neutral-800/90 border-neutral-600 text-neutral-100'
                        : 'bg-neutral-950/40 border-neutral-800/80 text-neutral-400 hover:bg-neutral-850 hover:text-neutral-200'
                    }`}
                  >
                    <div className="flex items-center gap-2">
                      <Palette size={14} className={settingsSubTab === 'appearance' ? 'text-emerald-400' : 'text-neutral-500'} />
                      <span className="font-medium text-[11px]">[SUBTAB_APPEARANCE]</span>
                    </div>
                    <ChevronRight size={12} className="text-neutral-600" />
                  </button>

                  <button
                    onClick={() => {
                      setSettingsSubTab('system')
                      handleResetSelection()
                    }}
                    className={`w-full flex items-center justify-between p-2 rounded border text-left transition-colors ${
                      settingsSubTab === 'system'
                        ? 'bg-neutral-800/90 border-neutral-600 text-neutral-100'
                        : 'bg-neutral-950/40 border-neutral-800/80 text-neutral-400 hover:bg-neutral-850 hover:text-neutral-200'
                    }`}
                  >
                    <div className="flex items-center gap-2">
                      <HardDrive size={14} className={settingsSubTab === 'system' ? 'text-purple-400' : 'text-neutral-500'} />
                      <span className="font-medium text-[11px]">[SUBTAB_SYSTEM]</span>
                    </div>
                    <ChevronRight size={12} className="text-neutral-600" />
                  </button>
                </div>
              ) : (
                /* Wireframe Layout Slots for other main tabs */
                <div className="space-y-2">
                  <div className="h-7 rounded border border-dashed border-neutral-800 bg-neutral-950/40 flex items-center px-2 text-neutral-600">
                    [SLOT_NAV_1]
                  </div>
                  <div className="h-28 rounded border border-dashed border-neutral-800/70 bg-neutral-950/20 flex items-center justify-center text-neutral-600">
                    [TREE / LIST AREA]
                  </div>
                </div>
              )}
            </div>

            {/* Left Sidebar Footer */}
            <div className="h-8 border-t border-neutral-800/80 px-3 flex items-center justify-between text-neutral-600 text-[10px]">
              <span>LEFT_DRAWER_FOOTER</span>
            </div>

            {/* Resizer Handle */}
            <div
              onMouseDown={() => {
                isDraggingLeft.current = true
                document.body.style.cursor = 'col-resize'
              }}
              className="absolute top-0 right-0 w-1 h-full cursor-col-resize hover:bg-sky-500/50 transition-colors z-10"
            />
          </aside>
        ) : (
          <div className="w-10 h-full bg-neutral-900 border-r border-neutral-800 flex flex-col items-center py-2 shrink-0">
            <button
              onClick={() => setLeftSidebarOpen(true)}
              className="p-1.5 rounded text-neutral-400 hover:text-neutral-100 hover:bg-neutral-800 transition-colors"
              title="Expand Sidebar"
            >
              <PanelLeftOpen size={16} />
            </button>
          </div>
        )}

        {/* ======================================================= */}
        {/* CONDITIONAL JOINED RIGHT PANEL (WHEN DEVICE CLICKED)   */}
        {/* Placed immediately beside left panel when joined        */}
        {/* ======================================================= */}
        {isRightJoinedWithLeft && rightPanelOpen && (
          <aside
            style={{ width: `${rightWidth}px` }}
            className="h-full bg-neutral-900 border-r border-neutral-800 flex flex-col shrink-0 select-none relative group transition-[width] duration-150 z-10"
          >
            {/* Top Bar for Joined Right Panel */}
            <div className="h-11 border-b border-neutral-800 bg-neutral-900/95 px-2 flex items-center justify-between shrink-0">
              <div className="flex items-center gap-1">
                <button
                  onClick={() => setRightPanelOpen(false)}
                  className="p-1.5 rounded text-neutral-500 hover:text-neutral-200 hover:bg-neutral-800 transition-colors mr-0.5"
                  title="Collapse Inspector"
                >
                  <PanelRightClose size={15} />
                </button>
                <button
                  onClick={handleResetSelection}
                  className="p-1.5 rounded text-emerald-400 hover:bg-emerald-950/40 hover:text-emerald-300 border border-emerald-900/40 transition-colors"
                  title="Accept Changes"
                >
                  <Check size={15} />
                </button>
                <button
                  onClick={handleResetSelection}
                  className="p-1.5 rounded text-rose-400 hover:bg-rose-950/40 hover:text-rose-300 border border-rose-900/40 transition-colors"
                  title="Decline Changes"
                >
                  <X size={15} />
                </button>
              </div>

              <div className="flex items-center gap-1 text-neutral-400">
                <span className="px-1.5 py-0.5 rounded bg-sky-950/40 text-sky-400 border border-sky-800/50 text-[10px] uppercase font-semibold">
                  JOINED_INSPECTOR
                </span>
              </div>
            </div>

            {/* Device Settings Form in Right Panel */}
            <div className="flex-1 p-2 space-y-2 overflow-y-auto">
              <div className="p-2 rounded border border-neutral-800 bg-neutral-950/50 flex items-center justify-between text-neutral-300">
                <span className="font-semibold text-[11px] flex items-center gap-1.5">
                  <SlidersHorizontal size={13} className="text-sky-400" />
                  <span>[SETTINGS_FORM: {selectedDeviceId?.toUpperCase()}]</span>
                </span>
                <span className="text-[10px] text-neutral-600 font-mono">[{rightWidth}px]</span>
              </div>

              <div className="h-20 rounded border border-dashed border-neutral-800 bg-neutral-950/40 p-2 flex flex-col justify-between text-neutral-600">
                <span className="text-[10px] text-neutral-400">[I2C_BUS_ADDR_SLOT]</span>
                <div className="h-6 w-full rounded border border-dashed border-neutral-800/80 bg-neutral-900/40 flex items-center px-2 text-[10px]">
                  [INPUT_REGISTER_CONFIG]
                </div>
              </div>

              <div className="h-28 rounded border border-dashed border-neutral-800 bg-neutral-950/40 p-2 flex flex-col justify-between text-neutral-600">
                <span className="text-[10px] text-neutral-400">[PIN_MAPPING_SLOT]</span>
                <div className="h-6 w-full rounded border border-dashed border-neutral-800/80 bg-neutral-900/40 flex items-center px-2 text-[10px]">
                  [OE_PIN / EN_PIN_ROUTE]
                </div>
                <div className="h-6 w-full rounded border border-dashed border-neutral-800/80 bg-neutral-900/40 flex items-center px-2 text-[10px]">
                  [INT_PIN_ASSIGNMENT]
                </div>
              </div>

              <div className="h-32 rounded border border-dashed border-neutral-800/80 bg-neutral-950/20 p-2 flex flex-col justify-center items-center text-neutral-600 text-center">
                <span>[CUSTOM_COMMAND_REGISTRY]</span>
                <span className="text-[9px] text-neutral-700 mt-1">DEVICE_INITIALIZATION_HOOKS</span>
              </div>
            </div>

            {/* Resizer Handle */}
            <div
              onMouseDown={() => {
                isDraggingRight.current = true
                document.body.style.cursor = 'col-resize'
              }}
              className="absolute top-0 right-0 w-1 h-full cursor-col-resize hover:bg-sky-500/50 transition-colors z-10"
            />
          </aside>
        )}

        {/* ======================================================= */}
        {/* 2. CENTER WINDOW: CONTENT ONLY                          */}
        {/*    DEVICES VIEW: INTERACTIVE BOARD SVG VISUALIZER       */}
        {/* ======================================================= */}
        <main
          onClick={handleResetSelection}
          className="flex-1 flex flex-col min-w-0 bg-neutral-950 relative overflow-hidden cursor-default"
        >
          {/* Subtle expand button if left panel is collapsed */}
          <div className="absolute top-2 left-2 z-10 flex gap-2">
            {!leftSidebarOpen && (
              <button
                onClick={(e) => {
                  e.stopPropagation()
                  setLeftSidebarOpen(true)
                }}
                className="p-1.5 rounded bg-neutral-900/80 border border-neutral-800 text-neutral-400 hover:text-neutral-100 shadow-md backdrop-blur transition-colors"
                title="Open Left Sidebar"
              >
                <PanelLeftOpen size={15} />
              </button>
            )}
          </div>

          {/* Full-height Content Wireframe Area */}
          <div className="flex-1 h-full w-full p-3 flex flex-col overflow-hidden">
            {leftTab === 'devices' ? (
              /* INTERACTIVE BOARD SVG VISUALIZER */
              <div
                onClick={handleResetSelection}
                className="flex-1 w-full h-full rounded border border-dashed border-neutral-800 bg-neutral-900/20 p-3 flex flex-col relative overflow-hidden"
              >
                {/* SVG Visualizer Header / Zoom Controls */}
                <div className="flex items-center justify-between pb-2 border-b border-neutral-800/80 mb-2 pointer-events-none">
                  <div className="flex items-center gap-2">
                    <CircuitBoard size={15} className="text-sky-400" />
                    <span className="text-neutral-400 font-semibold tracking-wider uppercase text-[11px]">
                      [INTERACTIVE_BOARD_PINOUT_SVG]
                    </span>
                  </div>

                  {/* Pan/Zoom Tools */}
                  <div className="flex items-center gap-1 pointer-events-auto">
                    <button
                      onClick={(e) => {
                        e.stopPropagation()
                        setZoomLevel(prev => Math.min(prev + 0.2, 2.5))
                      }}
                      className="p-1 rounded bg-neutral-900 border border-neutral-800 text-neutral-400 hover:text-neutral-200"
                      title="Zoom In"
                    >
                      <ZoomIn size={13} />
                    </button>
                    <button
                      onClick={(e) => {
                        e.stopPropagation()
                        setZoomLevel(prev => Math.max(prev - 0.2, 0.6))
                      }}
                      className="p-1 rounded bg-neutral-900 border border-neutral-800 text-neutral-400 hover:text-neutral-200"
                      title="Zoom Out"
                    >
                      <ZoomOut size={13} />
                    </button>
                    <button
                      onClick={(e) => {
                        e.stopPropagation()
                        setZoomLevel(1)
                      }}
                      className="p-1 rounded bg-neutral-900 border border-neutral-800 text-neutral-400 hover:text-neutral-200"
                      title="Reset View"
                    >
                      <Crosshair size={13} />
                    </button>
                    <span className="text-[10px] text-neutral-500 ml-1 font-mono">
                      {Math.round(zoomLevel * 100)}%
                    </span>
                  </div>
                </div>

                {/* SVG Board Canvas Surface and Vertical Action Toolbar */}
                <div className="flex-1 w-full h-full flex overflow-hidden relative">
                  <div
                    className="flex-1 h-full flex items-center justify-center overflow-hidden relative cursor-default"
                  >
                    <svg
                      style={{ transform: `scale(${zoomLevel})` }}
                      viewBox="0 0 800 500"
                    className="w-full h-full max-w-4xl max-h-[480px] transition-transform duration-200"
                  >
                    {/* PCB Board Outline */}
                    <rect
                      x="40"
                      y="30"
                      width="720"
                      height="440"
                      rx="20"
                      className="fill-neutral-900/90 stroke-neutral-750 stroke-2"
                    />

                    {/* Ground Plane Grid / PCB Trace Grid Lines */}
                    <pattern id="grid" width="20" height="20" patternUnits="userSpaceOnUse">
                      <path d="M 20 0 L 0 0 0 20" fill="none" stroke="#262626" strokeWidth="0.5" />
                    </pattern>
                    <rect x="40" y="30" width="720" height="440" rx="20" fill="url(#grid)" opacity="0.6" />

                    {/* Mounting Holes */}
                    <circle cx="65" cy="55" r="10" className="fill-neutral-950 stroke-neutral-700 stroke-2" />
                    <circle cx="735" cy="55" r="10" className="fill-neutral-950 stroke-neutral-700 stroke-2" />
                    <circle cx="65" cy="445" r="10" className="fill-neutral-950 stroke-neutral-700 stroke-2" />
                    <circle cx="735" cy="445" r="10" className="fill-neutral-950 stroke-neutral-700 stroke-2" />

                    {/* MCU Chip (ESP32-S3) Center Block */}
                    <g className="cursor-pointer">
                      <rect
                        x="330"
                        y="180"
                        width="140"
                        height="140"
                        rx="8"
                        className="fill-neutral-850 stroke-neutral-700 stroke-2"
                      />
                      <text x="400" y="245" textAnchor="middle" fill="#9ca3af" fontSize="12" fontFamily="monospace" fontWeight="bold">
                        ESP32-S3
                      </text>
                      <text x="400" y="265" textAnchor="middle" fill="#6b7280" fontSize="9" fontFamily="monospace">
                        CORE_MCU
                      </text>
                    </g>

                    {/* Interactive Device IC 1: PCA9685 */}
                    <g
                      onClick={(e) => handleDeviceClick('dev-pca9685', e as unknown as React.MouseEvent)}
                      className="cursor-pointer group/ic"
                    >
                      <rect
                        x="100"
                        y="90"
                        width="150"
                        height="85"
                        rx="6"
                        className={`transition-colors stroke-2 ${
                          selectedDeviceId === 'dev-pca9685'
                            ? 'fill-sky-950/80 stroke-sky-400'
                            : 'fill-neutral-850 stroke-neutral-700 hover:fill-neutral-800 hover:stroke-sky-400/60'
                        }`}
                      />
                      <circle cx="115" cy="105" r="3" className="fill-sky-400" />
                      <text x="175" y="130" textAnchor="middle" fill="#e5e7eb" fontSize="11" fontFamily="monospace" fontWeight="bold">
                        PCA9685
                      </text>
                      <text x="175" y="148" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="monospace">
                        0x40 (I2C-1)
                      </text>
                    </g>

                    {/* Interactive Device IC 2: TPS55289 */}
                    <g
                      onClick={(e) => handleDeviceClick('dev-tps55289', e as unknown as React.MouseEvent)}
                      className="cursor-pointer group/ic"
                    >
                      <rect
                        x="100"
                        y="230"
                        width="150"
                        height="85"
                        rx="6"
                        className={`transition-colors stroke-2 ${
                          selectedDeviceId === 'dev-tps55289'
                            ? 'fill-sky-950/80 stroke-sky-400'
                            : 'fill-neutral-850 stroke-neutral-700 hover:fill-neutral-800 hover:stroke-sky-400/60'
                        }`}
                      />
                      <circle cx="115" cy="245" r="3" className="fill-emerald-400" />
                      <text x="175" y="270" textAnchor="middle" fill="#e5e7eb" fontSize="11" fontFamily="monospace" fontWeight="bold">
                        TPS55289
                      </text>
                      <text x="175" y="288" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="monospace">
                        0x74 (I2C-1)
                      </text>
                    </g>

                    {/* Interactive Device IC 3: TCA6424A */}
                    <g
                      onClick={(e) => handleDeviceClick('dev-tca6424a', e as unknown as React.MouseEvent)}
                      className="cursor-pointer group/ic"
                    >
                      <rect
                        x="550"
                        y="90"
                        width="150"
                        height="85"
                        rx="6"
                        className={`transition-colors stroke-2 ${
                          selectedDeviceId === 'dev-tca6424a'
                            ? 'fill-sky-950/80 stroke-sky-400'
                            : 'fill-neutral-850 stroke-neutral-700 hover:fill-neutral-800 hover:stroke-sky-400/60'
                        }`}
                      />
                      <circle cx="565" cy="105" r="3" className="fill-amber-400" />
                      <text x="625" y="130" textAnchor="middle" fill="#e5e7eb" fontSize="11" fontFamily="monospace" fontWeight="bold">
                        TCA6424A
                      </text>
                      <text x="625" y="148" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="monospace">
                        0x22 (I2C-1)
                      </text>
                    </g>

                    {/* Interactive Device IC 4: INA226 */}
                    <g
                      onClick={(e) => handleDeviceClick('dev-ina226', e as unknown as React.MouseEvent)}
                      className="cursor-pointer group/ic"
                    >
                      <rect
                        x="550"
                        y="230"
                        width="150"
                        height="85"
                        rx="6"
                        className={`transition-colors stroke-2 ${
                          selectedDeviceId === 'dev-ina226'
                            ? 'fill-sky-950/80 stroke-sky-400'
                            : 'fill-neutral-850 stroke-neutral-700 hover:fill-neutral-800 hover:stroke-sky-400/60'
                        }`}
                      />
                      <circle cx="565" cy="245" r="3" className="fill-purple-400" />
                      <text x="625" y="270" textAnchor="middle" fill="#e5e7eb" fontSize="11" fontFamily="monospace" fontWeight="bold">
                        INA226
                      </text>
                      <text x="625" y="288" textAnchor="middle" fill="#9ca3af" fontSize="9" fontFamily="monospace">
                        0x45 (I2C-1)
                      </text>
                    </g>

                    {/* Bottom Expansion Header Pinout Strip */}
                    <rect x="180" y="390" width="440" height="45" rx="4" className="fill-neutral-950 stroke-neutral-750 stroke-1" />
                    <text x="400" y="418" textAnchor="middle" fill="#737373" fontSize="10" fontFamily="monospace">
                      [HEADER_EXPANSION_GPIO_I2C_BUS_0]
                    </text>
                  </svg>
                </div>

                {/* Vertical Plugs / Actions Toolbar on the Right */}
                <div
                  onClick={(e) => e.stopPropagation()}
                  className="w-11 h-full py-2 bg-neutral-900/90 border-l border-neutral-800 flex flex-col items-center gap-1.5 shrink-0 select-none z-10"
                >
                  <button
                    title="Connect"
                    onClick={() => setRightTab('connect')}
                    className={`p-2 rounded transition-colors ${
                      rightTab === 'connect'
                        ? 'bg-neutral-800 text-sky-400 shadow-sm'
                        : 'text-neutral-400 hover:text-sky-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Plug size={16} />
                  </button>

                  <div className="w-5 h-px bg-neutral-800 my-0.5" />

                  <button
                    title="Run"
                    onClick={() => setRightTab('run')}
                    className={`p-2 rounded transition-colors ${
                      rightTab === 'run'
                        ? 'bg-neutral-800 text-emerald-400 shadow-sm'
                        : 'text-neutral-400 hover:text-emerald-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Play size={16} />
                  </button>

                  <button
                    title="Stop"
                    onClick={() => setRightTab('stop')}
                    className={`p-2 rounded transition-colors ${
                      rightTab === 'stop'
                        ? 'bg-neutral-800 text-rose-400 shadow-sm'
                        : 'text-neutral-400 hover:text-rose-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Square size={15} />
                  </button>

                  <button
                    title="Watch"
                    onClick={() => setRightTab('watch')}
                    className={`p-2 rounded transition-colors ${
                      rightTab === 'watch'
                        ? 'bg-neutral-800 text-amber-400 shadow-sm'
                        : 'text-neutral-400 hover:text-amber-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Eye size={16} />
                  </button>
                </div>
              </div>
            </div>
            ) : leftTab === 'settings' ? (
              /* Config Wireframe Slots in Main Content */
              <div
                onClick={handleResetSelection}
                className="flex-1 w-full h-full rounded border border-dashed border-neutral-800 bg-neutral-900/20 p-4 flex flex-col overflow-y-auto cursor-default"
              >
                <div className="flex items-center justify-between pb-3 border-b border-neutral-800/80 mb-4 pointer-events-none">
                  <span className="text-neutral-400 font-semibold tracking-wider uppercase text-[11px]">
                    [CONFIG_LAYOUT_VIEW: {settingsSubTab.toUpperCase()}]
                  </span>
                  <span className="text-neutral-600 text-[10px]">
                    {selectedConfigElement
                      ? '(Click outside any box to return to normal state)'
                      : '(Click any box to extend right panel)'}
                  </span>
                </div>

                <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
                  {[1, 2, 3, 4, 5, 6].map(item => (
                    <div
                      key={item}
                      onClick={(e) => handleConfigElementClick(item, e)}
                      className={`p-3 rounded border cursor-pointer transition-all flex flex-col justify-between h-28 select-none ${
                        selectedConfigElement === item
                          ? 'border-sky-500 bg-sky-950/25 shadow-sm ring-1 ring-sky-500/40'
                          : 'border-dashed border-neutral-800 bg-neutral-950/40 hover:border-neutral-700 hover:bg-neutral-900/50'
                      }`}
                    >
                      <div className="flex items-center justify-between pointer-events-none">
                        <span className="text-neutral-400 font-medium">
                          [CONFIG_ELEMENT_SLOT_{item}]
                        </span>
                        <ChevronRight
                          size={14}
                          className={selectedConfigElement === item ? 'text-sky-400' : 'text-neutral-600'}
                        />
                      </div>
                      <div className="h-6 w-full rounded border border-dashed border-neutral-800/80 bg-neutral-900/30 flex items-center px-2 text-neutral-600 text-[10px] pointer-events-none">
                        [LAYOUT_INPUT_SLOT]
                      </div>
                      <div className="flex items-center justify-between text-[9px] text-neutral-600 pointer-events-none">
                        <span>SELECT_TO_INSPECT</span>
                        {selectedConfigElement === item && (
                          <span className="text-sky-400 font-bold">EXTENDED_IN_RIGHT_PANEL</span>
                        )}
                      </div>
                    </div>
                  ))}
                </div>
              </div>
            ) : (
              /* Default other views */
              <div className="flex-1 w-full h-full rounded border border-dashed border-neutral-800 bg-neutral-900/20 flex flex-col items-center justify-center text-center p-4">
                <span className="text-neutral-600 tracking-widest text-[11px] uppercase">
                  [CENTER_CONTENT_AREA: {leftTab.toUpperCase()}]
                </span>
              </div>
            )}
          </div>
        </main>

        {/* ======================================================= */}
        {/* 3. RIGHT COLUMN: STANDARD RIGHT POSITION                */}
        {/*    (ACTIVE WHEN NOT IN DEVICES VIEW)                    */}
        {/* ======================================================= */}
        {leftTab !== 'devices' && (
          rightPanelOpen ? (
            <aside
              style={{ width: `${rightWidth}px` }}
              className="h-full bg-neutral-900 border-l border-neutral-800 flex flex-col shrink-0 select-none relative group transition-[width] duration-150"
            >
              {/* Resizer Handle */}
              <div
                onMouseDown={() => {
                  isDraggingRight.current = true
                  document.body.style.cursor = 'col-resize'
                }}
                className="absolute top-0 left-0 w-1 h-full cursor-col-resize hover:bg-sky-500/50 transition-colors z-10"
              />

              {/* Top Bar */}
              <div className="h-11 border-b border-neutral-800 bg-neutral-900/95 px-2 flex items-center justify-between shrink-0">
                <div className="flex items-center gap-1">
                  <button
                    onClick={() => setRightPanelOpen(false)}
                    className="p-1.5 rounded text-neutral-500 hover:text-neutral-200 hover:bg-neutral-800 transition-colors mr-0.5"
                    title="Collapse Right Panel"
                  >
                    <PanelRightClose size={15} />
                  </button>

                  <button
                    onClick={handleResetSelection}
                    className="p-1.5 rounded text-emerald-400 hover:bg-emerald-950/40 hover:text-emerald-300 border border-emerald-900/40 transition-colors"
                    title="Accept Changes"
                  >
                    <Check size={15} />
                  </button>

                  <button
                    onClick={handleResetSelection}
                    className="p-1.5 rounded text-rose-400 hover:bg-rose-950/40 hover:text-rose-300 border border-rose-900/40 transition-colors"
                    title="Decline Changes"
                  >
                    <X size={15} />
                  </button>
                </div>

                <div className="flex items-center gap-1">
                  <button
                    title="Connect"
                    onClick={() => setRightTab('connect')}
                    className={`p-1.5 rounded transition-colors ${
                      rightTab === 'connect'
                        ? 'bg-neutral-800 text-sky-400 shadow-sm'
                        : 'text-neutral-400 hover:text-sky-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Plug size={15} />
                  </button>

                  <div className="h-4 w-px bg-neutral-800 mx-0.5" />

                  <button
                    title="Run"
                    onClick={() => setRightTab('run')}
                    className={`p-1.5 rounded transition-colors ${
                      rightTab === 'run'
                        ? 'bg-neutral-800 text-emerald-400 shadow-sm'
                        : 'text-neutral-400 hover:text-emerald-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Play size={15} />
                  </button>

                  <button
                    title="Stop"
                    onClick={() => setRightTab('stop')}
                    className={`p-1.5 rounded transition-colors ${
                      rightTab === 'stop'
                        ? 'bg-neutral-800 text-rose-400 shadow-sm'
                        : 'text-neutral-400 hover:text-rose-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Square size={14} />
                  </button>

                  <button
                    title="Watch"
                    onClick={() => setRightTab('watch')}
                    className={`p-1.5 rounded transition-colors ${
                      rightTab === 'watch'
                        ? 'bg-neutral-800 text-amber-400 shadow-sm'
                        : 'text-neutral-400 hover:text-amber-400 hover:bg-neutral-850'
                    }`}
                  >
                    <Eye size={15} />
                  </button>

                  <div className="h-4 w-px bg-neutral-800 mx-0.5" />

                  <button
                    title="Right Panel Settings"
                    onClick={() => setRightTab('settings')}
                    className={`p-1.5 rounded transition-colors ${
                      rightTab === 'settings'
                        ? 'bg-neutral-800 text-neutral-100 shadow-sm'
                        : 'text-neutral-400 hover:text-neutral-200 hover:bg-neutral-850'
                    }`}
                  >
                    <SlidersHorizontal size={15} />
                  </button>
                </div>
              </div>

              {/* Right Panel Body Slots */}
              <div className="flex-1 p-2 space-y-2 overflow-y-auto">
                {rightTab === 'settings' ? (
                  <div className="flex flex-col gap-2 h-full">
                    <div className="p-2 rounded border border-neutral-800 bg-neutral-950/50 flex items-center justify-between text-neutral-400">
                      <span className="font-semibold text-[11px] flex items-center gap-1.5">
                        <SlidersHorizontal size={13} className="text-sky-400" />
                        <span>
                          {selectedConfigElement
                            ? `[DETAIL_SLOT_${selectedConfigElement}]`
                            : '[CONFIG_INSPECTOR]'}
                        </span>
                      </span>
                      <span className="text-[10px] text-neutral-600 font-mono">[{rightWidth}px]</span>
                    </div>

                    <div className="h-24 rounded border border-dashed border-neutral-800 bg-neutral-950/40 flex items-center justify-center text-neutral-600">
                      [EXTENDED_PARAM_SLOT_1]
                    </div>
                    <div className="h-32 rounded border border-dashed border-neutral-800 bg-neutral-950/40 flex items-center justify-center text-neutral-600">
                      [EXTENDED_PARAM_SLOT_2]
                    </div>
                    <div className="flex-1 rounded border border-dashed border-neutral-800/70 bg-neutral-950/20 flex items-center justify-center text-neutral-600">
                      [EXTENDED_OPTIONS_SLOT]
                    </div>
                  </div>
                ) : (
                  <>
                    <div className="h-20 rounded border border-dashed border-neutral-800 bg-neutral-950/40 flex items-center justify-center text-neutral-600">
                      [PROPERTIES_GROUP_1]
                    </div>
                    <div className="h-32 rounded border border-dashed border-neutral-800 bg-neutral-950/40 flex items-center justify-center text-neutral-600">
                      [PROPERTIES_GROUP_2]
                    </div>
                    <div className="h-44 rounded border border-dashed border-neutral-800/60 bg-neutral-950/20 flex items-center justify-center text-neutral-600">
                      [CONFIG_DETAILS_GROUP]
                    </div>
                  </>
                )}
              </div>

              {/* Right Panel Footer */}
              <div className="h-8 border-t border-neutral-800/80 px-3 flex items-center justify-between text-neutral-600 text-[10px]">
                <span>RIGHT_PANEL_FOOTER</span>
              </div>
            </aside>
          ) : (
            <div className="w-10 h-full bg-neutral-900 border-l border-neutral-800 flex flex-col items-center py-2 shrink-0">
              <button
                onClick={() => setRightPanelOpen(true)}
                className="p-1.5 rounded text-neutral-400 hover:text-neutral-100 hover:bg-neutral-800 transition-colors"
                title="Expand Right Panel"
              >
                <PanelRightOpen size={16} />
              </button>
            </div>
          )
        )}

      </div>

      {/* ========================================================= */}
      {/* 4. STATUS BAR (FOOTER)                                    */}
      {/* ========================================================= */}
      <footer className="h-6 border-t border-neutral-800 bg-neutral-950 px-3 flex items-center justify-between text-[10px] text-neutral-500 select-none z-20 shrink-0">
        <div className="flex items-center gap-3">
          <div className="flex items-center gap-1.5">
            <span className="w-1.5 h-1.5 rounded-full bg-emerald-500" />
            <span className="text-neutral-400">LAYOUT_READY</span>
          </div>
          <span className="text-neutral-700">|</span>
          <span>
            LEFT: {leftSidebarOpen ? `${leftTab.toUpperCase()}${leftTab === 'settings' ? ` (${settingsSubTab})` : ''} [${leftWidth}px]` : 'COLLAPSED'}
          </span>
          <span className="text-neutral-700">|</span>
          <span>
            INSPECTOR: {isRightJoinedWithLeft ? `JOINED_LEFT [${rightWidth}px]` : leftTab === 'devices' ? 'HIDDEN (NO DEVICE)' : rightPanelOpen ? `RIGHT [${rightWidth}px]` : 'COLLAPSED'}
          </span>
        </div>

        <div className="flex items-center gap-3 text-neutral-500">
          <span>
            {selectedDeviceId
              ? `DEVICE: ${selectedDeviceId.toUpperCase()}`
              : selectedConfigElement
              ? `CONFIG: SLOT_${selectedConfigElement}`
              : 'CANVAS_VIEWPORT'}
          </span>
          <span className="text-neutral-700">|</span>
          <span>PURE_WIREFRAME</span>
        </div>
      </footer>

    </div>
  )
}
