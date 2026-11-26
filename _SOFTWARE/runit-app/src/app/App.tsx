import { ArrowLeftRight, Bluetooth, BluetoothConnected, BluetoothOff, CircleHelp, ClipboardPaste, Code2, Copy, Cpu, FolderOpen, HardDriveDownload, HardDriveUpload, Moon, PanelLeft, Pause, PanelRight, Bug, Eye, Pencil, PenTool, Play, Redo2, RotateCcw, RotateCw, ScrollText, Settings, SlidersHorizontal, SquareDashedMousePointer, Sun, Trash2, Undo2, Workflow } from 'lucide-react'
import { bundledServiceCatalog } from '@backend'
import type { LinkState } from '@backend'
import { Badge, HelpOverlay, PanelHeader, SegmentedSwitch } from '@ui/components'
import { usePersistedChoice, usePersistedFlag } from '@ui/hooks/useStorage'
import { DetailsPanel, ExplorerPanel, MainScreen, ResizeHandle, WorkspaceShell } from '@ui/layout/WorkspaceLayout'
import { usePanels } from '@ui/layout/usePanels'
import { ConnectionPanel } from './connection/ConnectionPanel'
import { useLink } from './connection/useLink'
import { useVm } from './useVm'
import { DebugPanel } from './run/DebugPanel'
import { useRunSettings } from './run/useRunSettings'
import { LiveContext } from './run/live'
import { ProjectScreen } from './project/ProjectScreen'
import { emptyProject } from './project/projectFile'
import { readStoredProject, useProject } from './project/useProject'
import { useEffect, useMemo, useState } from 'react'
import { DevicesExplorer } from './devices/DevicesExplorer'
import { DevicesScreen } from './devices/DevicesScreen'
import { DeviceTestPanel, PinPanel } from './devices/DeviceTestPanel'
import { useDevices } from './devices/useDevices'
import { boardDeviceId, deviceList } from './devices/devices'
import { readStoredBoard, useBoardProfile } from './devices/useBoardProfile'
import { BoardProfileScreen } from './devices/BoardProfileScreen'
import { setProviderName, useBoardSettings } from './settings/useBoardSettings'
import { AppSettingsScreen } from './appSettings/AppSettingsScreen'
import { useAppZoom } from './appSettings/useAppZoom'
import { SettingsScreen } from './settings/SettingsScreen'
import { APP_TAB, POWER_TAB, SettingsTabs, settingsTabOf, tabSections } from './settings/SettingsTabs'
import { BOARD_TAB, BOARDS_TAB, CONNECTIONS_TAB, PROJECT_TAB, ProjectTabs } from './project/ProjectTabs'
import { useRemote } from './remote/useRemote'
import type { RemoteState } from './remote/useRemote'
import type { Perspective } from './remote/remote'
import { useRouting } from './routing/useRouting'
import { buildProviderRegistry } from './routing/providerRegistry'
import { ConnectionsScreen } from './routing/ConnectionsScreen'
import { uartProviderNames } from './routing/routing'
import { BoardsScreen } from './boards/BoardsScreen'
import { CONSOLE_SHOWS, ConsoleExplorer, ConsoleScreen } from './console/ConsoleScreen'
import type { ConsoleShow } from './console/ConsoleScreen'
import { ConsoleContext, useConsole } from './console/useConsole'
import type { ConsoleJumps } from './console/useConsole'
import { draftForVariable, findVariable, toPath } from '@engine'
import { useCode } from './code/useCode'
import { pathOf } from './code/variables'
import { VariableDetails, VariablesEditor } from './code/VariableScreen'
import { VariablesExplorer } from './code/VariablesExplorer'
import { useBlocks } from './code/useBlocks'
import { BlockPalette } from './code/BlockPalette'
import { BlockDetails } from './code/BlockDetails'
import { BlocksList } from './code/BlocksList'
import { CanvasSurface } from './code/canvas/CanvasSurface'
import { PowerLive } from './power/PowerLive'
import { SafeStateButton, SafeStateNotice } from './SafeState'
import { PowerDisplayScreen } from './power/PowerDisplayScreen'
import { defaultReadings } from './power/powerDisplay'
import type { CodeState } from './code/useCode'
import { useHardware } from './hardware/useHardware'
import { HardwareCanvas } from './hardware/HardwareCanvas'
import { HardwarePalette } from './hardware/HardwarePalette'
import { HardwareDetails } from './hardware/HardwareDetails'
import { PartCreator } from './hardware/PartCreator'

/** The screens, as tabs along the top of the left panel (the Console's on the right). */
const VIEWS = [
  { name: 'Project', icon: FolderOpen, title: 'Project', note: 'The project file and what the board is made of.' },
  { name: 'Code', icon: Code2, title: 'Code', note: 'Variables, blocks and the canvas.' },
  { name: 'Devices', icon: Cpu, title: 'Devices', note: 'The board\'s devices, their pins and commands.' },
  { name: 'Settings', icon: Settings, title: 'Settings', note: 'The board\'s settings and the app\'s.' },
  { name: 'Console', icon: ScrollText, title: 'Console', note: 'The logs and errors of the board.' },
] as const
type ViewName = (typeof VIEWS)[number]['name']

/**
 * Code and Devices each have two screens, switched by the first button of the action bar: the details (lists and forms) and the
 * canvas (a drawing): Code's canvas holds the blocks, Devices' the board and the parts wired to it (hardware/).
 */
const SCREEN_MODES = ['details', 'canvas'] as const
type ScreenMode = (typeof SCREEN_MODES)[number]
const SCREENS: Partial<Record<ViewName, Record<ScreenMode, string>>> = {
  Code: { details: 'The variables and the blocks.', canvas: 'The blocks and the flow between them.' },
  Devices: { details: 'The devices, their pins and commands.', canvas: 'The board and what is connected to it.' },
}

/** The Devices canvas (hardware/) is off for now: Devices shows its details only, with no button to switch. */
const DEVICES_CANVAS = false

/** What the left panel of the Code view lists: the variables, or the block palette. */
const CODE_PALETTES = [['variables', 'Variables'], ['blocks', 'Blocks']] as const
type CodePalette = (typeof CODE_PALETTES)[number][0]

/** The left panel's tabs. The Console is not among them: its tab is on the right of the frame (`ViewButton`), with its red dot. */
const LEFT_VIEWS = VIEWS.filter((v) => v.name !== 'Console')

/** One screen's tab. `unread`: the console's unread errors, a red dot that blinks in the icon's corner. */
function ViewButton({ name, selected, unread = 0, onSelect }: { name: ViewName; selected: boolean; unread?: number; onSelect: () => void }) {
  const { icon: Icon, title } = VIEWS.find((v) => v.name === name)!
  return (
    <button className={`view-tab ${selected ? 'selected' : ''}`.trim()} role="tab" aria-selected={selected} title={unread ? `${title}: ${unread} errors not read` : title} aria-label={title} onClick={onSelect}>
      <Icon aria-hidden="true" />
      {unread ? <span className="view-mark" aria-hidden="true" /> : null}
    </button>
  )
}

function ViewTabs({ view, onView }: { view: ViewName; onView: (view: ViewName) => void }) {
  return (
    <div className="view-menu" role="tablist" aria-label="Current view">
      {LEFT_VIEWS.map(({ name }) => <ViewButton key={name} name={name} selected={view === name} onSelect={() => onView(name)} />)}
    </div>
  )
}

/** The top of the Project view's left panel: the project's name and whether it is saved; a click opens its landing page. */
function ProjectSummary({ name, unsaved, selected, onOpen }: { name: string; unsaved: boolean; selected: boolean; onOpen: () => void }) {
  return (
    <div className={`project-summary ${selected ? 'is-selected' : ''}`.trim()} role="button" tabIndex={0} aria-pressed={selected} title="The project: its file, what it holds, what the board holds"
      onClick={onOpen} onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); onOpen() } }}>
      <PanelHeader title={name}>{unsaved && <Badge tone="warning">unsaved</Badge>}</PanelHeader>
    </div>
  )
}

/** How the connection button looks in each state of the link (colours: styles/status.css). */
const LINK_VIEW: Record<LinkState, { tone: string; Icon: typeof Bluetooth; title: string }> = {
  connected: { tone: 'status-ok', Icon: BluetoothConnected, title: 'Bluetooth: connected' },
  reconnecting: { tone: 'status-lost', Icon: BluetoothOff, title: 'Bluetooth: connection lost, reconnecting…' },
  connecting: { tone: 'status-warn', Icon: Bluetooth, title: 'Bluetooth: connecting…' },
  idle: { tone: 'status-warn', Icon: Bluetooth, title: 'Bluetooth: not connected' },
  closed: { tone: 'status-warn', Icon: BluetoothOff, title: 'Bluetooth: closed' },
}

/**
 * The app: the connection to the main board, the board behind it (remote/), and the workspace of the one shown. The
 * workspace is made anew when the other board is shown, so nothing of one board is on the screens of the other.
 */
export function App() {
  const [auto, setAuto] = usePersistedFlag('runit.autoReconnect', true)
  const main = useLink(auto)
  const [picked, setPerspective] = useState<Perspective>('main')
  const [pendingRemoteDevice, setPendingRemoteDevice] = useState<number | undefined>()
  const remote = useRemote(main.link, main.state, picked)
  const perspective: Perspective = remote.on ? picked : 'main'
  return (
    <Workspace
      key={perspective}
      perspective={perspective}
      onPerspective={setPerspective}
      onOpenRemoteDevice={(remoteId) => { setPendingRemoteDevice(remoteId); setPerspective('remote') }}
      initialDevice={perspective === 'remote' ? pendingRemoteDevice : undefined}
      onConsumeInitialDevice={() => setPendingRemoteDevice(undefined)}
      main={main}
      remote={remote}
      auto={auto}
      onAuto={setAuto}
    />
  )
}

/** The screens over one board: the main one, or (`perspective` remote) the board behind it, by that board's own link. */
function Workspace({ perspective, onPerspective, onOpenRemoteDevice, initialDevice, onConsumeInitialDevice, main, remote, auto, onAuto }: {
  perspective: Perspective
  onPerspective: (perspective: Perspective) => void
  onOpenRemoteDevice: (remoteId: number) => void
  initialDevice?: number
  onConsumeInitialDevice: () => void
  main: ReturnType<typeof useLink>
  remote: RemoteState
  auto: boolean
  onAuto: (on: boolean) => void
}) {
  const behind = perspective === 'remote'
  const link = behind ? remote.link : main.link
  const state = behind ? remote.state : main.state
  const { reason, board: myBoard, searching, available, scan, choose, cancelScan, connect, disconnect, forget } = main
  const panels = usePanels()
  const project = useProject(perspective)
  const runSettings = useRunSettings()
  const vm = useVm(link, state, project.project.program, runSettings.holdMs)
  const code = useCode(project)
  const [theme, setTheme] = usePersistedChoice('runit.theme', ['dark', 'light'] as const, 'dark')
  const [view, setView] = usePersistedChoice<ViewName>('runit.shell.view', ['Project', 'Code', 'Devices', 'Settings', 'Console'], 'Project')
  const [codeMode, setCodeMode] = usePersistedChoice<ScreenMode>('runit.code.mode', SCREEN_MODES, 'details')
  const [storedDevicesMode, setDevicesMode] = usePersistedChoice<ScreenMode>('runit.devices.mode', SCREEN_MODES, 'details')
  const devicesMode: ScreenMode = DEVICES_CANVAS ? storedDevicesMode : 'details'
  const [connectionOpen, setConnectionOpen] = useState(false)
  // The bug button's panel (debug) takes the right panel the way the connection does; one covers the other.
  const [debugOpen, setDebugOpen] = useState(false)
  const covered = connectionOpen || debugOpen
  // Help mode for this session: numbered notes over the fields and buttons of the screen (HelpOverlay).
  const [help, setHelp] = useState(false)
  const [autoSync, setAutoSync] = usePersistedFlag('runit.autoSync', true)
  const boardProfile = useBoardProfile(perspective)
  // On the main board, hydrate wired remote aliases with the remote board's stored profile & pin setup so Code filters pins accurately.
  const remoteContext = useMemo(() => {
    if (behind) return undefined
    const catalog = bundledServiceCatalog()
    const remoteBoard = readStoredBoard('remote', catalog)
    const remoteProject = readStoredProject('remote') ?? emptyProject()
    return { board: remoteBoard, devices: deviceList(remoteProject, catalog, remoteBoard) }
  }, [behind, remote.state, project.project.boards])
  const devices = useDevices(link, state, project, autoSync, boardProfile, 'app', remoteContext, !behind ? remote.reattachSlave : undefined)
  // When switching to the remote board by clicking a ghost device on the master, open that device right away.
  useEffect(() => {
    if (initialDevice === undefined) return
    setView('Devices')
    devices.select({ kind: 'device', id: initialDevice })
    onConsumeInitialDevice()
  }, [initialDevice, setView, devices.select, onConsumeInitialDevice])
  // Opens a remote ghost device on its own board when the wired remote link is active; returns false otherwise.
  const openRemoteDevice = (boardId: number, remoteDeviceId: number): boolean => {
    if (behind || !remote.on || main.state !== 'connected') return false
    const isWiredBoard = project.project.boards?.some((b) => b.id === boardId && b.link === 'wired')
      || remote.slavePeer?.peerId === boardId
      || remote.link.session?.boardId === boardId
    if (!isWiredBoard) return false
    onOpenRemoteDevice(remoteDeviceId)
    return true
  }
  // The UART ports of the board and the project: devices of the UART contract, what the Console's terminal talks to.
  const uartPorts = useMemo(() => devices.devices.filter((d) => d.type?.id === 'esp32s3_uart').map((d) => ({ id: d.id, name: d.name })), [devices.devices])
  // The Project view's board editor: the same Devices state over the board's profile alone.
  const boardEditor = useDevices(link, state, boardProfile, false, boardProfile, 'board')
  const blocks = useBlocks(project, code.engine, devices)
  const hardware = useHardware(project, devices)
  const [codePalette, setCodePalette] = usePersistedChoice<CodePalette>('runit.code.palette', CODE_PALETTES.map(([key]) => key), 'variables')
  // A variable picked in the centre (its entry) is what the right panel shows; one picked in the tree feeds the selected block's pins.
  const pickVariable: CodeState = { ...code, select: (id) => { blocks.select(undefined); code.select(id) } }
  // The variable picked in the tree, as a path: on the canvas a tap on a block offers its pins for it.
  const pickedDraft = code.selected ? draftForVariable(code.tree, code.selected.variable.id) : undefined
  const pickedPath = pickedDraft && toPath(pickedDraft)
  // The board's settings, read as soon as the link is up (the Settings view shows them).
  const boardSettings = useBoardSettings(link, state === 'connected', project)
  // A provider is called by the user's name, else (a framed UART's) by its device's.
  const providerNames = useMemo(() => ({ ...uartProviderNames(devices.devices), ...project.project.providerNames }), [devices.devices, project.project.providerNames])
  const routing = useRouting(link, state === 'connected', providerNames)
  const providers = useMemo(() => buildProviderRegistry({ live: routing.snapshot.providers, project: project.project, devices: devices.devices }), [routing.snapshot.providers, project.project, devices.devices])
  const settingTabs = tabSections(boardSettings.sections, link.defs.keys.project)
  // The Project view opens on its landing page (the project); a board tab is a page reached from it, not remembered.
  const [projectTab, setProjectTab] = useState<string>(PROJECT_TAB)
  // The Settings view's tab: remembered for the session; one gone (its service not described) falls back to the first.
  const [pickedSettingsTab, setSettingsTab] = useState<string>('')
  const settingsTab = settingsTabOf(pickedSettingsTab, settingTabs)
  // The board editor (Project > Board profile) is the Devices view over the board's profile: the toolbar, the panels and the centre act on it.
  const boardEdit = view === 'Project' && projectTab === BOARD_TAB
  const profileEdit = boardEdit
  const ed = profileEdit ? boardEditor : devices
  const showView = (next: ViewName) => {
    if (next === 'Project' && view !== 'Project') setProjectTab(PROJECT_TAB)
    setView(next)
  }
  const appZoom = useAppZoom()
  const tabSection = settingTabs.find((s) => s.service.id === settingsTab)
  // What the Live power display reads: the project's, else the board's default.
  const powerReadings = useMemo(() => project.project.powerDisplay ?? defaultReadings(devices.board), [project.project.powerDisplay, devices.board])
  // The board's logs and errors (Console view); the errors are also shown on what they are about (devices, pins, settings tabs).
  const board = useConsole(link, state, devices.interpreter)
  const [consoleShow, setConsoleShow] = usePersistedChoice<ConsoleShow>('runit.console.show', CONSOLE_SHOWS, 'all')
  const [consoleSubject, setConsoleSubject] = useState<string | undefined>()
  const jumps: ConsoleJumps = {
    target: (ref) => {
      // An object of the program on the board that is a variable of the project: the Code view, with it selected.
      const found = board.interpreter.target(ref) as { kind?: string; variable?: string } | undefined
      if (found?.kind === 'variable' && found.variable !== undefined && findVariable(code.tree, found.variable)) {
        const id = found.variable
        return { kind: 'other', name: pathOf(code.tree, id) ?? id, open: () => { setView('Code'); pickVariable.select(id) } }
      }
      // A block of the program: the Code view, with it selected.
      const block = found as { kind?: string; block?: string } | undefined
      if (block?.kind === 'block' && block.block !== undefined && blocks.program.blocks.some((b) => b.id === block.block)) {
        const id = block.block
        return { kind: 'other', name: blocks.nameOf(id), open: () => { setView('Code'); blocks.select(id) } }
      }
      const device = (id: number) => devices.devices.find((d) => d.id === id)
      if (ref.kind === 'device') {
        const d = device(ref.id)
        return d && { kind: 'device', name: d.name, open: () => { setView('Devices'); devices.select({ kind: 'device', id: d.id }) } }
      }
      if ((ref.kind === 'pin' || ref.kind === 'channel') && ref.scope?.kind === 'device') {
        const d = device(ref.scope.id)
        if (!d) return undefined
        return ref.kind === 'pin'
          ? { kind: 'device', name: d.name, detail: devices.pinName(d.id, ref.id), open: () => { setView('Devices'); devices.selectPin(d.id, ref.id) } }
          : { kind: 'device', name: d.name, detail: `Channel ${ref.id}`, open: () => { setView('Devices'); devices.select({ kind: 'device', id: d.id }) } }
      }
      if (ref.kind === 'router_key') {
        if (ref.id === link.defs.keys.project) return { kind: 'settings', name: 'Project', open: () => { setView('Project'); setProjectTab(PROJECT_TAB) } }
        const s = settingTabs.find((t) => t.service.key === ref.id)
        return s && { kind: 'settings', name: s.title, open: () => { setView('Settings'); setSettingsTab(s.service.id) } }
      }
      return undefined
    },
  }
  const pinContext = { devices: devices.devices, board: devices.board }
  const nameOf = (id: number) => devices.devices.find((d) => d.id === id)?.name ?? 'Board'
  // The Devices view's test panel: the selected device's operations, in the details panel while the connection is not open there.
  // On its canvas the panel shows what is selected there instead (a part, a pin, a wire, the board's power).
  const onDevicesCanvas = view === 'Devices' && devicesMode === 'canvas'
  const testDevice = (view === 'Devices' && !onDevicesCanvas) || boardEdit ? ed.selectedDevice : undefined
  const hardwareKey = onDevicesCanvas && hardware.selection && !hardware.creator ? `hw:${hardware.selection.kind}:${'id' in hardware.selection ? hardware.selection.id : hardware.selection.key}` : undefined
  const testId = testDevice?.id
  // The Code view's details panel: the selected block, else the selected variable.
  const codeBlock = view === 'Code' ? blocks.selected?.id : undefined
  const codeVariable = view === 'Code' && codeBlock === undefined ? code.selected?.variable.id : undefined
  const detailsKey = hardwareKey ?? (testId !== undefined ? `device:${testId}` : undefined) ?? (codeBlock !== undefined ? `block:${codeBlock}` : codeVariable !== undefined ? `variable:${codeVariable}` : undefined)
  // The details panel is open while it has something to show: a device or a variable chosen opens its panel (in place of the
  // connection panel), nothing left to show closes it.
  const showsNothing = !covered && detailsKey === undefined
  // On a canvas (Code's, Devices') a selection opens nothing, so the canvas and the left panel never move under the pointer: the
  // panel follows the selection while it is open, and a double-click on a block or a part or the toolbar's pencil opens it, or
  // hides it when it already shows that block (`toggleDetails`).
  const onCodeCanvas = view === 'Code' && codeMode === 'canvas'
  const onCanvas = onCodeCanvas || onDevicesCanvas
  const openDetails = () => {
    setConnectionOpen(false); setDebugOpen(false)
    panels.setRightOpen(true)
  }
  const toggleDetails = (block?: string) => {
    const showing = panels.rightOpen && !covered && detailsKey !== undefined && (block === undefined || codeBlock === block)
    setConnectionOpen(false); setDebugOpen(false)
    panels.setRightOpen(!showing)
  }
  useEffect(() => {
    if (detailsKey === undefined || onCanvas) return
    setConnectionOpen(false); setDebugOpen(false)
    panels.setRightOpen(true)
  }, [detailsKey])
  useEffect(() => {
    if (showsNothing) panels.setRightOpen(false)
  }, [showsNothing])
  // A click in the Devices view's panel of the selected device brings its live test back in the right panel, over the connection
  // or the debug panel (a new selection does it by itself, above).
  const showDeviceTest = () => {
    if (testDevice === undefined || (panels.rightOpen && !covered)) return
    setConnectionOpen(false); setDebugOpen(false)
    panels.setRightOpen(true)
  }

  // The connection button is the main board's, whichever board is shown: the Bluetooth is its.
  const { tone, Icon: LinkIcon, title: linkTitle } = available ? LINK_VIEW[main.state] : { tone: 'status-warn', Icon: BluetoothOff, title: 'This browser has no Web Bluetooth' }
  // The details panel is open only while it has something to show (so far the connection panel); closing that collapses it.
  const closeDetails = () => {
    setConnectionOpen(false); setDebugOpen(false)
    panels.setRightOpen(false)
  }
  const openConnection = () => {
    // Open over the test panel: closing it goes back to the test panel.
    if (connectionOpen && panels.rightOpen) return detailsKey !== undefined ? setConnectionOpen(false) : closeDetails()
    setDebugOpen(false)
    setConnectionOpen(true)
    panels.setRightOpen(true)
    panels.widenRight()
  }
  // The bug button: its panel opens in the right panel (a second press closes it, back to what was there).
  const openDebug = () => {
    if (debugOpen && panels.rightOpen) return detailsKey !== undefined ? setDebugOpen(false) : closeDetails()
    setConnectionOpen(false)
    setDebugOpen(true)
    panels.setRightOpen(true)
    panels.widenRight()
  }
  // Which board the app shows: while a wired remote board is enabled, main view links it to the master and remote view opens the app tunnel.
  const remoteReady = remote.mode === 'master' ? !!remote.slavePeer?.open : remote.state === 'connected'
  const remoteTone = remoteReady ? 'status-ok' : remote.state === 'reconnecting' ? 'status-lost' : 'status-warn'
  const perspectiveButton = remote.on && (
    <button className={`${remoteTone} ${behind ? 'selected' : ''}`.trim()} aria-label="Remote board" aria-pressed={behind}
      data-help="Switches between the main board (linked to the slave) and the remote board's own view."
      title={behind ? 'Showing the remote board: back to the main board (hands the UART back to master)' : `Show the remote board${remoteReady ? ' (linked to master)' : ''}`}
      onClick={() => onPerspective(behind ? 'main' : 'remote')}>
      <ArrowLeftRight aria-hidden="true" />
    </button>
  )
  const current = VIEWS.find((v) => v.name === view)!
  const screens = SCREENS[view]
  const [mode, setMode] = view === 'Devices' ? [devicesMode, setDevicesMode] : [codeMode, setCodeMode]

  // The right toolbar. Open header: Run (Pause while the program runs), the eye, the bug, Write, the console, the connection, and
  // the safe stop last while the program runs. Collapsed: the corner of the header is always the connection; the column under it is
  // Run / Pause, the eye, the bug, Write and the console, and the safe stop at its foot while the program runs. Nothing moves when
  // it appears. Write sends the project to the board from every view; Read (into the project) stays in the action bar.
  const runButtons = (
    <>
      {vm.running ? (
        <button className="status-lost" aria-label="Pause" data-help="Pauses the program on the board; Run goes on from where it paused." disabled={!vm.canStop}
          title={vm.stale ? 'Pause the program: it changed, Run then loads the new one' : vm.real ? 'Pause the program on the board' : 'Pause (a simulation)'} onClick={() => void vm.stop()}>
          <Pause aria-hidden="true" />
        </button>
      ) : (
        <button className={vm.canRun ? 'status-ok' : ''} aria-label="Run" data-help="Loads the program into the board and runs it; while it runs, this is Stop." disabled={!vm.canRun}
          title={!vm.canRun ? 'Connect the board to run the program' : 'Load the program into the board and run it'} onClick={() => void vm.run('run')}>
          <Play aria-hidden="true" />
        </button>
      )}
      <button className={vm.live.mode === 'live' ? 'selected' : ''} aria-label="Live view" aria-pressed={vm.live.mode === 'live'} data-help="Shows which blocks act and what the variables and the outputs hold, as it happens. On a running program it only starts watching it." disabled={!vm.canWatch}
        title={vm.live.mode === 'live' ? 'Watching the program live (press to stop watching; it keeps running)' : !vm.canWatch ? 'Connect the board to watch the program' : vm.running && !vm.stale ? 'Watch the running program live' : 'Run the program and watch it live'} onClick={() => void vm.watchLive()}>
        <Eye aria-hidden="true" />
      </button>
      <button className={vm.live.mode === 'debug' || (debugOpen && panels.rightOpen) ? 'selected' : ''} aria-label="Debug" aria-pressed={debugOpen && panels.rightOpen} data-help="Debug: step by step or loop by loop, in slow motion, with every block's output."
        title="Debug: stepping, slow motion and every block's output" onClick={openDebug}>
        <Bug aria-hidden="true" />
      </button>
      <button aria-label="Write to the board" data-help="Writes the project to the board: the settings it keeps, its devices, their start commands, the program and the actions." title={state === 'connected' ? (ed.pending ? 'Write to the board: there are changes to write (the devices, their start commands, the program and the actions)' : 'Write to the board: the devices, their start commands, the program and the actions (the stored project)') : 'Connect the board first'}
        className={ed.pending ? 'toolbar-pending' : undefined} disabled={state !== 'connected' || ed.busy !== undefined} onClick={ed.write}>
        <HardDriveUpload aria-hidden="true" />
      </button>
    </>
  )
  const safeStopButton = (
    <button className="status-lost safe-stop" aria-label="Safe stop" title="Safe stop: the board stops the program and switches off everything" disabled={!vm.canSafeStop} onClick={() => void vm.safeStop()}>
      <span className="safe-stop-label" aria-hidden="true">STOP</span>
    </button>
  )
  const linkButton = (
    <button className={`link-toggle ${tone} ${connectionOpen && panels.rightOpen ? 'selected' : ''}`} aria-label="Bluetooth" data-help="The connection to the board: connect, scan for one, or forget it." title={linkTitle} aria-pressed={connectionOpen && panels.rightOpen} onClick={openConnection}>
      <LinkIcon aria-hidden="true" />
    </button>
  )
  // The Console's tab, on the right beside Run: its red dot blinks while an error is not read.
  const consoleButton = (
    <ViewButton name="Console" selected={view === 'Console'} unread={board.unread()} onSelect={() => { setView('Console'); panels.setLeftOpen(true) }} />
  )

  return (
    <ConsoleContext.Provider value={{ console: board, jumps }}>
    <LiveContext.Provider value={vm.live}>
    <WorkspaceShell className={`theme-${theme} ${panels.resizing ? 'is-resizing' : ''} ${behind ? 'is-remote' : ''}`}>
      <ExplorerPanel open={panels.leftOpen} width={panels.left}>
        <div className="side-panel-header left-strip">
          {panels.leftOpen && <ViewTabs view={view} onView={showView} />}
          <button className="panel-toggle" aria-label={panels.leftOpen ? 'Collapse left panel' : 'Expand left panel'} onClick={() => panels.setLeftOpen(!panels.leftOpen)}>
            <PanelLeft aria-hidden="true" />
          </button>
        </div>
        {panels.leftOpen
          ? (
            <div className="side-panel-body">
              {boardEdit
                ? (
                  <>
                    <DevicesExplorer key="board-editor" state={boardEditor} onBack={() => setProjectTab(PROJECT_TAB)} />
                  </>
                )
                : view === 'Project'
                ? (
                  <>
                    <ProjectSummary name={project.project.name} unsaved={project.unsaved} selected={projectTab === PROJECT_TAB} onOpen={() => setProjectTab(PROJECT_TAB)} />
                    <ProjectTabs tab={projectTab} onTab={(tab) => { if (tab === BOARD_TAB) boardEditor.select(undefined); setProjectTab(tab) }} />
                  </>
                )
                : view === 'Settings'
                ? <SettingsTabs sections={settingTabs} state={boardSettings} tab={settingsTab} onTab={setSettingsTab} />
                : view === 'Code'
                ? (
                  <>
                    <div className="code-palette-switch"><SegmentedSwitch label="The left panel lists" value={codePalette} options={CODE_PALETTES} onChange={setCodePalette} /></div>
                    {codePalette === 'blocks' ? <BlockPalette blocks={blocks} /> : <VariablesExplorer code={code} />}
                  </>
                )
                : view === 'Devices'
                ? (devicesMode === 'canvas' ? <HardwarePalette state={hardware} /> : <DevicesExplorer state={devices} onOpenRemoteDevice={openRemoteDevice} />)
                : view === 'Console'
                ? <ConsoleExplorer state={board} jumps={jumps} subject={consoleSubject} onSubject={setConsoleSubject} />
                : <div className="view-placeholder"><h2>{current.title}</h2><p>{current.note}</p></div>}
            </div>
          )
          : <div className="side-panel-icons"><ViewTabs view={view} onView={(next) => { showView(next); panels.setLeftOpen(true) }} /></div>}
      </ExplorerPanel>

      {panels.leftOpen && (
        <ResizeHandle side="left" label="Resize left panel" width={panels.left} onStart={() => panels.setResizing(true)} onEnd={() => panels.setResizing(false)} onResize={panels.dragLeft} />
      )}

      <div className="center-column">
        <MainScreen
          label="Main screen"
          toolbar={
            <>
              {screens && (view !== 'Devices' || DEVICES_CANVAS) && (
                // One button, a full touch target: it shows the screen it switches to.
                <button aria-label={mode === 'canvas' ? 'Show the details' : 'Show the canvas'} data-help="Switches between the lists and settings of this screen and its drawing." title={mode === 'canvas' ? 'Details: lists and settings' : 'Canvas: the drawing'} onClick={() => setMode(mode === 'canvas' ? 'details' : 'canvas')}>
                  {mode === 'canvas' ? <SlidersHorizontal aria-hidden="true" /> : <Workflow aria-hidden="true" />}
                </button>
              )}
              <button aria-label="Undo" data-help="Undoes the last change (Ctrl+Z)." title="Undo (Ctrl+Z)" disabled={!(profileEdit ? boardProfile.canUndo : project.canUndo)} onClick={profileEdit ? boardProfile.undo : project.undo}><Undo2 aria-hidden="true" /></button>
              <button aria-label="Redo" data-help="Redoes the change that was undone (Ctrl+Y)." title="Redo (Ctrl+Y)" disabled={!(profileEdit ? boardProfile.canRedo : project.canRedo)} onClick={profileEdit ? boardProfile.redo : project.redo}><Redo2 aria-hidden="true" /></button>
              {(view === 'Devices' || view === 'Project' || view === 'Settings') && (
                <>
                  <button aria-label="Read from the board" data-help="Reads the settings, devices, start commands and actions the board keeps into the project. Undo goes back." title={state === 'connected' ? 'Read from the board: the devices, their start commands and the actions it keeps (Undo goes back)' : 'Connect the board first'}
                    disabled={state !== 'connected' || ed.busy !== undefined} onClick={ed.read}>
                    <HardDriveDownload aria-hidden="true" />
                  </button>
                  {((view === 'Devices' && !onDevicesCanvas) || boardEdit) && (
                    <button className="toolbar-danger" aria-label="Remove the device from the project" data-help="Removes the selected device from the project. It leaves the board at the next write." title="Remove the selected device from the project (it leaves the board at the next write)"
                      disabled={!ed.selectedDevice || ed.selectedDevice.system} onClick={() => ed.selectedDevice && ed.removeDevice(ed.selectedDevice.id)}>
                      <Trash2 aria-hidden="true" />
                    </button>
                  )}
                  {onDevicesCanvas && (
                    <>
                      <button aria-label="Make a part" data-help="Opens the part creator: a devkit, a module or a board of your own, traced from a picture." title="The part creator: a devkit, a module, a board of your own"
                        className={hardware.creator ? 'selected' : undefined} aria-pressed={!!hardware.creator} onClick={() => (hardware.creator ? hardware.closeCreator() : hardware.openCreator())}>
                        <PenTool aria-hidden="true" />
                      </button>
                      <button aria-label="Turn left" data-help="Turns the selected part, the board, or the part being placed a quarter to the left (Shift+R or Shift+Space on the canvas does the same)." title="Turn left (Shift+R or Shift+Space)"
                        disabled={!hardware.canTurn || !!hardware.creator} onClick={() => hardware.turnSelected(-90)}>
                        <RotateCcw aria-hidden="true" />
                      </button>
                      <button aria-label="Turn right" data-help="Turns the selected part, the board, or the part being placed a quarter to the right (R or Space on the canvas does the same)." title="Turn right (R or Space)"
                        disabled={!hardware.canTurn || !!hardware.creator} onClick={() => hardware.turnSelected(90)}>
                        <RotateCw aria-hidden="true" />
                      </button>
                      <button aria-label="Edit the selection" data-help="Opens the details of the selected part, pin or wire in the right panel, or hides them; a double-click on a part does the same." title="Edit: show or hide the details of the selection (double-click a part)"
                        className={panels.rightOpen && !covered && detailsKey !== undefined ? 'selected' : undefined} aria-pressed={panels.rightOpen && !covered && detailsKey !== undefined} disabled={detailsKey === undefined} onClick={() => toggleDetails()}>
                        <Pencil aria-hidden="true" />
                      </button>
                    </>
                  )}
                  {onDevicesCanvas && (
                    <button className="toolbar-danger" aria-label="Remove the selection from the canvas" data-help="Removes the selected part (with its wires and its device), wire, or the wires of the selected pin (Delete on the canvas does the same)." title="Remove the selected part, wire or pin's wires (Delete)"
                      disabled={!hardware.selection || (hardware.selection.kind === 'item' && hardware.selection.id === 'board')} onClick={() => hardware.remove()}>
                      <Trash2 aria-hidden="true" />
                    </button>
                  )}
                </>
              )}
              {view === 'Code' && mode === 'canvas' && (
                <button aria-label="Select" data-help="The select tool: a drag on the canvas draws a box round blocks, as with Shift, instead of moving the canvas. Click again to move the canvas." title={blocks.selecting ? 'Select: drag a box round blocks (click to move the canvas again)' : 'Select: drag a box round blocks (or hold Shift)'}
                  className={blocks.selecting ? 'selected' : undefined} aria-pressed={blocks.selecting} onClick={() => blocks.setSelecting(!blocks.selecting)}>
                  <SquareDashedMousePointer aria-hidden="true" />
                </button>
              )}
              {view === 'Code' && (
                <button aria-label="Copy the selected blocks" data-help="Copies the selected blocks (Ctrl+C on the canvas does the same)." title="Copy the selected blocks (Ctrl+C)"
                  disabled={blocks.selectedIds.length === 0} onClick={blocks.copy}>
                  <Copy aria-hidden="true" />
                </button>
              )}
              {view === 'Code' && (
                <button aria-label="Paste the copied blocks" data-help="Pastes the copied blocks, a little further down and right each time (Ctrl+V on the canvas does the same)." title="Paste the copied blocks (Ctrl+V)"
                  disabled={!blocks.canPaste} onClick={blocks.paste}>
                  <ClipboardPaste aria-hidden="true" />
                </button>
              )}
              {view === 'Code' && (
                <button aria-label="Edit the selected block" data-help="Opens the details of the selected block (or variable) in the right panel, or hides them; a double-click on a block does the same." title="Edit: show or hide the details of the selection (double-click a block)"
                  className={panels.rightOpen && !covered && detailsKey !== undefined ? 'selected' : undefined} aria-pressed={panels.rightOpen && !covered && detailsKey !== undefined} disabled={detailsKey === undefined} onClick={() => toggleDetails()}>
                  <Pencil aria-hidden="true" />
                </button>
              )}
              {view === 'Code' && (
                <button className="toolbar-danger" aria-label="Remove the selected blocks" data-help="Removes the selected blocks from the program (Delete on the canvas does the same)." title="Remove the selected blocks (Delete)"
                  disabled={blocks.selectedIds.length === 0} onClick={() => blocks.remove(blocks.selectedIds)}>
                  <Trash2 aria-hidden="true" />
                </button>
              )}
              {behind && <span className="remote-mark" title="Everything here is the board wired to the main one, reached through it">Remote board</span>}
              <span className="toolbar-spacer" />
              {ed.busy && <span className="bar-note" role="status">{ed.busy.step}… {ed.busy.done}/{ed.busy.total}</span>}
              {!ed.busy && ed.note && <span className={`bar-note ${ed.note.tone === 'error' ? 'is-error' : ''}`} role="status" title={ed.note.text} onClick={ed.clearNote}>{ed.note.text}</span>}
              {vm.progress && <span className="bar-note" role="status">{vm.progress}</span>}
              {vm.error && <span className="bar-note is-error" role="status" title={vm.error}>{vm.error}</span>}
              <button className={`help-toggle ${help ? 'selected' : ''}`.trim()} aria-label="Help" aria-pressed={help} title={help ? 'Hide the help' : 'Help: outlines on the fields and buttons of this screen, a click shows what each does'} onClick={() => setHelp(!help)}>
                <CircleHelp aria-hidden="true" />
              </button>
              <button aria-label={theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'} title={theme === 'dark' ? 'Light theme' : 'Dark theme'} onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}>
                {theme === 'dark' ? <Sun aria-hidden="true" /> : <Moon aria-hidden="true" />}
              </button>
            </>
          }
        >
          {view === 'Console'
            ? <ConsoleScreen state={board} jumps={jumps} show={consoleShow} onShow={setConsoleShow} subject={consoleSubject} onSubject={setConsoleSubject} link={link} connected={state === 'connected'} ports={uartPorts} />
            : view === 'Devices' && mode === 'details'
            ? <div className="devices-focus" onClick={showDeviceTest}><DevicesScreen state={devices} onOpenRemoteDevice={openRemoteDevice} onOpenBoards={() => { setView('Project'); setProjectTab(BOARDS_TAB) }} /></div>
            : view === 'Code' && mode === 'details'
            ? (codePalette === 'blocks' ? <BlocksList blocks={blocks} /> : <VariablesEditor code={pickVariable} />)
            : view === 'Devices' && mode === 'canvas'
            ? (hardware.creator ? <PartCreator key={hardware.creator.part?.id ?? 'new'} state={hardware} catalog={devices.catalog} board={devices.board} /> : <HardwareCanvas state={hardware} onOpenDetails={openDetails} />)
            : view === 'Code'
            ? <CanvasSurface blocks={blocks} picked={pickedPath} onPickedUsed={() => code.select(undefined)} onToggleDetails={toggleDetails} />
            : screens
            ? <div className={`view-placeholder ${mode === 'canvas' ? 'canvas-empty' : ''}`}><h2>{current.title} · {mode === 'canvas' ? 'Canvas' : 'Details'}</h2><p>{screens[mode]}</p></div>
            : view === 'Settings'
              ? (settingsTab === POWER_TAB
                ? <PowerDisplayScreen project={project} services={devices.services} board={devices.board} devices={devices.devices} connected={state === 'connected'} />
                : settingsTab === APP_TAB || !tabSection
                ? <AppSettingsScreen zoom={appZoom} />
                : <SettingsScreen section={tabSection} state={boardSettings} context={pinContext} nameOf={nameOf} registry={providers} />)
            : view === 'Project'
              ? (projectTab === BOARD_TAB
                ? (boardEditor.selection
                  ? <div className="devices-focus" onClick={showDeviceTest}><DevicesScreen state={boardEditor} /></div>
                  : <BoardProfileScreen profile={boardProfile} editor={boardEditor} />)
                : projectTab === CONNECTIONS_TAB
                ? <ConnectionsScreen registry={providers} routing={routing} onName={(id, name) => setProviderName(project, id, name)} />
                : projectTab === BOARDS_TAB
                ? <BoardsScreen
                    registry={providers}
                    project={project}
                    link={link}
                    services={devices.services}
                    board={devices.board}
                    connected={state === 'connected'}
                    {...(!behind ? { remote, mainConnected: main.state === 'connected', onOpenRemote: () => onPerspective('remote') } : {})}
                    onOpenDevice={(id) => {
                      const d = devices.devices.find((o) => o.id === id)
                      if (d?.board !== undefined && openRemoteDevice(d.board, boardDeviceId(d))) return
                      setView('Devices')
                      devices.select({ kind: 'device', id })
                    }}
                    onAddDevice={(boardId) => { setView('Devices'); devices.select({ kind: 'add', board: boardId }) }}
                  />
                : <ProjectScreen project={project} link={link} connected={state === 'connected'} />)
              : <div className="view-placeholder"><h2>{current.title}</h2><p>Nothing here yet.</p></div>}
        </MainScreen>
      </div>

      {panels.rightOpen && (
        <ResizeHandle side="right" label="Resize right panel" width={panels.right} onStart={() => panels.setResizing(true)} onEnd={() => panels.setResizing(false)} onResize={panels.dragRight} />
      )}

      <DetailsPanel open={panels.rightOpen} className={vm.safe ? 'is-safe-state' : undefined} style={panels.rightOpen ? { width: panels.right } : undefined}>
        <div className="side-panel-header right-header">
          {panels.rightOpen
            ? (
              <>
                <button className="panel-toggle" aria-label="Collapse right panel" onClick={closeDetails}>
                  <PanelRight aria-hidden="true" />
                </button>
                <div className="header-icons">{runButtons}{consoleButton}{perspectiveButton}{linkButton}{vm.running && safeStopButton}</div>
              </>
            )
            : linkButton}
        </div>
        {panels.rightOpen
          ? (
            <div className="side-panel-body">
              {vm.safe && (
                <SafeStateNotice safe={vm.safe} faultLatched={vm.board?.faultLatched ?? false} faultDevice={vm.board?.faultDevice === undefined ? undefined : devices.nameOf(vm.board.faultDevice)}
                  busy={!vm.canSafeStop} programReady={vm.real} onLeave={() => void vm.leaveSafe()} onAckFault={() => void vm.ackFault()} />
              )}
              <PowerLive layout="row" services={devices.services} board={devices.board} devices={devices.devices} readings={powerReadings} connected={state === 'connected'} />
              {!covered && testDevice && (ed.pinSelection?.device === testDevice.id
                ? <PinPanel key={`${boardEdit ? 'board' : 'app'}:${testDevice.id}/${ed.pinSelection.pin}`} device={testDevice} pin={ed.pinSelection.pin} state={ed} />
                : <DeviceTestPanel key={`${boardEdit ? 'board' : 'app'}:${testDevice.id}`} device={testDevice} state={ed} />)}
              {!covered && hardwareKey !== undefined && <HardwareDetails state={hardware} onOpenDevice={(id) => { setDevicesMode('details'); devices.select({ kind: 'device', id }) }} />}
              {!covered && codeBlock !== undefined && <BlockDetails key={codeBlock} blocks={blocks} code={code} />}
              {!covered && codeVariable !== undefined && <VariableDetails code={code} />}
              {debugOpen && !connectionOpen && <DebugPanel settings={runSettings} canStart={vm.canWatch} stale={vm.stale} running={vm.running} stepping={vm.stepping} watching={vm.live.mode === 'debug'} {...(vm.progress ? { progress: vm.progress } : {})}
                {...(state !== 'connected' ? { why: 'Connect the board to debug the program.' } : {})}
                onStart={() => void vm.run('debug', runSettings.slow, runSettings.stepping, runSettings.stepMs)} onStepping={(how) => void vm.control(how === 'continuous' ? 'normal' : how === 'loop' ? 'scan' : 'block')}
                onNext={() => void vm.control(vm.stepping === 'step' ? 'next' : 'once')} onRewind={() => void vm.control('restart')} onSlow={(f) => void vm.slowTo(f, runSettings.stepMs)} onStepMs={(ms) => void vm.slowTo(runSettings.slow, ms)} />}
              {connectionOpen && <ConnectionPanel state={main.state} board={myBoard} searching={searching} reason={reason} available={available} scan={scan} onChoose={choose} onCancelScan={cancelScan} auto={auto} onAuto={onAuto} sync={autoSync} onSync={setAutoSync} onConnect={() => void connect()} onDisconnect={() => void disconnect()} onForget={() => void forget()} />}
            </div>
          )
          : <div className="side-panel-icons">{vm.safe && <SafeStateButton safe={vm.safe} onOpen={() => { setDebugOpen(false); setConnectionOpen(true); panels.setRightOpen(true) }} />}{perspectiveButton}{runButtons}{consoleButton}<PowerLive layout="column" services={devices.services} board={devices.board} devices={devices.devices} readings={powerReadings} connected={state === 'connected'} />{vm.running && <div className="safe-stop-dock">{safeStopButton}</div>}</div>}
      </DetailsPanel>
      <HelpOverlay on={help} />
    </WorkspaceShell>
    </LiveContext.Provider>
    </ConsoleContext.Provider>
  )
}
