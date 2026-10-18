import { Bluetooth, BluetoothConnected, BluetoothOff, CircleHelp, Code2, Cpu, FolderOpen, HardDriveDownload, HardDriveUpload, Moon, OctagonX, PanelLeft, PanelRight, Play, Redo2, ScrollText, SlidersHorizontal, Square, Sun, Trash2, Undo2, Workflow } from 'lucide-react'
import type { LinkState } from '@backend'
import { Badge, HelpOverlay, PanelHeader } from '@ui/components'
import { usePersistedChoice, usePersistedFlag } from '@ui/hooks/useStorage'
import { DetailsPanel, ExplorerPanel, MainScreen, ResizeHandle, WorkspaceShell } from '@ui/layout/WorkspaceLayout'
import { usePanels } from '@ui/layout/usePanels'
import { ConnectionPanel } from './ConnectionPanel'
import { useLink } from './useLink'
import { useVm } from './useVm'
import { ProjectScreen } from './project/ProjectScreen'
import { projectCounts } from './project/projectFile'
import { useProject } from './project/useProject'
import { useEffect, useState } from 'react'
import { DevicesExplorer } from './devices/DevicesExplorer'
import { DevicesScreen } from './devices/DevicesScreen'
import { DeviceTestPanel, PinPanel } from './devices/DeviceTestPanel'
import { useDevices } from './devices/useDevices'
import { useBoardSettings } from './settings/useBoardSettings'
import { SettingsScreen } from './settings/SettingsScreen'
import { PROJECT_TAB, SettingsTabs, tabSections } from './settings/SettingsTabs'
import { CONSOLE_SHOWS, ConsoleExplorer, ConsoleScreen } from './console/ConsoleScreen'
import type { ConsoleShow } from './console/ConsoleScreen'
import { ConsoleContext, useConsole } from './console/useConsole'
import type { ConsoleJumps } from './console/useConsole'
import { findVariable } from '@engine'
import { useCode } from './code/useCode'
import { pathOf } from './code/variables'
import { VariableDetails, VariablesEditor } from './code/VariableScreen'
import { VariablesExplorer } from './code/VariablesExplorer'

/** The screens, as tabs along the top of the left panel (the Console's on the right). */
const VIEWS = [
  { name: 'Project', icon: FolderOpen, title: 'Project', note: 'The project file and the board settings.' },
  { name: 'Code', icon: Code2, title: 'Code', note: 'Variables, blocks and the canvas.' },
  { name: 'Devices', icon: Cpu, title: 'Devices', note: 'The board\'s devices, their pins and commands.' },
  { name: 'Console', icon: ScrollText, title: 'Console', note: 'The logs and errors of the board.' },
] as const
type ViewName = (typeof VIEWS)[number]['name']

/**
 * Code and Devices each have two screens, switched by the first button of the action bar: the details (lists and forms) and the
 * canvas (a drawing). The details of Code (the variables) and of Devices are written; the canvases say what they will hold.
 */
const SCREEN_MODES = ['details', 'canvas'] as const
type ScreenMode = (typeof SCREEN_MODES)[number]
const SCREENS: Partial<Record<ViewName, Record<ScreenMode, string>>> = {
  Code: { details: 'The variables and the blocks.', canvas: 'The blocks and the flow between them.' },
  Devices: { details: 'The devices, their pins and commands.', canvas: 'The board and what is connected to it.' },
}

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

/** The left panel of the Project view: the project's name, whether it is saved, what it holds. */
function ProjectSummary({ name, unsaved, counts }: { name: string; unsaved: boolean; counts: ReturnType<typeof projectCounts> }) {
  return (
    <div className="project-summary">
      <PanelHeader title={name}>{unsaved && <Badge tone="warning">unsaved</Badge>}</PanelHeader>
      <p>{counts.variables} variables · {counts.blocks} blocks · {counts.devices} devices · {counts.actions} actions</p>
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

export function App() {
  const [auto, setAuto] = usePersistedFlag('runit.autoReconnect', true)
  const { link, state, reason, device, available, scan, choose, cancelScan, connect, disconnect, forget } = useLink(auto)
  const vm = useVm(link, state)
  const panels = usePanels()
  const project = useProject()
  const code = useCode(project)
  const [theme, setTheme] = usePersistedChoice('runit.theme', ['dark', 'light'] as const, 'dark')
  const [view, setView] = usePersistedChoice<ViewName>('runit.shell.view', ['Project', 'Code', 'Devices', 'Console'], 'Project')
  const [codeMode, setCodeMode] = usePersistedChoice<ScreenMode>('runit.code.mode', SCREEN_MODES, 'details')
  const [devicesMode, setDevicesMode] = usePersistedChoice<ScreenMode>('runit.devices.mode', SCREEN_MODES, 'details')
  const [connectionOpen, setConnectionOpen] = useState(false)
  // Help mode for this session: numbered notes over the fields and buttons of the screen (HelpOverlay).
  const [help, setHelp] = useState(false)
  const [autoSync, setAutoSync] = usePersistedFlag('runit.autoSync', true)
  const devices = useDevices(link, state, project, autoSync)
  // The board's settings, read as soon as the link is up (the Project screen shows them).
  const boardSettings = useBoardSettings(link, state === 'connected', project)
  const settingTabs = tabSections(boardSettings.sections, link.defs.keys.project)
  const [projectTab, setProjectTab] = usePersistedChoice<string>('runit.project.tab', [PROJECT_TAB, ...settingTabs.map((s) => s.service.id)], PROJECT_TAB)
  const tabSection = settingTabs.find((s) => s.service.id === projectTab)
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
        return { kind: 'other', name: pathOf(code.tree, id) ?? id, open: () => { setView('Code'); code.select(id) } }
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
        const tab = ref.id === link.defs.keys.project ? { id: PROJECT_TAB, name: 'Project' } : (() => {
          const s = settingTabs.find((t) => t.service.key === ref.id)
          return s && { id: s.service.id, name: s.title }
        })()
        return tab && { kind: 'settings', name: tab.name, open: () => { setView('Project'); setProjectTab(tab.id) } }
      }
      return undefined
    },
  }
  const pinContext = { devices: devices.devices, board: devices.board }
  const nameOf = (id: number) => devices.devices.find((d) => d.id === id)?.name ?? 'Board'
  // The Devices view's test panel: the selected device's operations, in the details panel while the connection is not open there.
  const testDevice = view === 'Devices' ? devices.selectedDevice : undefined
  const testId = testDevice?.id
  // The Code view's details panel: the selected variable.
  const codeVariable = view === 'Code' ? code.selected?.variable.id : undefined
  const detailsKey = testId !== undefined ? `device:${testId}` : codeVariable !== undefined ? `variable:${codeVariable}` : undefined
  // The details panel is open while it has something to show: a device or a variable chosen opens its panel (in place of the
  // connection panel), nothing left to show closes it.
  const showsNothing = !connectionOpen && detailsKey === undefined
  useEffect(() => {
    if (detailsKey === undefined) return
    setConnectionOpen(false)
    panels.setRightOpen(true)
  }, [detailsKey])
  useEffect(() => {
    if (showsNothing) panels.setRightOpen(false)
  }, [showsNothing])

  const { tone, Icon: LinkIcon, title: linkTitle } = available ? LINK_VIEW[state] : { tone: 'status-warn', Icon: BluetoothOff, title: 'This browser has no Web Bluetooth' }
  // The details panel is open only while it has something to show (so far the connection panel); closing that collapses it.
  const closeDetails = () => {
    setConnectionOpen(false)
    panels.setRightOpen(false)
  }
  const openConnection = () => {
    // Open over the test panel: closing it goes back to the test panel.
    if (connectionOpen && panels.rightOpen) return detailsKey !== undefined ? setConnectionOpen(false) : closeDetails()
    setConnectionOpen(true)
    panels.setRightOpen(true)
    panels.widenRight()
  }
  const current = VIEWS.find((v) => v.name === view)!
  const screens = SCREENS[view]
  const [mode, setMode] = view === 'Devices' ? [devicesMode, setDevicesMode] : [codeMode, setCodeMode]

  // The right toolbar. Open header: Run, Stop, the connection, and the safe stop in the corner while the program runs. Collapsed:
  // the corner of the header is the connection, or the safe stop while the program runs; the column under it is Run and Stop,
  // with the connection on top of them while the safe stop has its corner.
  const runButtons = (
    <>
      <button className={vm.canRun ? 'status-ok' : ''} aria-label="Run" data-help="Runs the program on the board." title={vm.running ? 'The program is running' : !vm.canRun ? 'Connect the board to run the program' : vm.real ? 'Run the program on the board' : 'Run (a simulation: the board has no program loaded)'} disabled={!vm.canRun} onClick={() => void vm.run()}>
        <Play aria-hidden="true" />
      </button>
      <button className={vm.canStop ? 'status-lost' : ''} aria-label="Stop" data-help="Pauses the program on the board." title={!vm.canStop ? 'The program is not running' : vm.real ? 'Pause the program on the board' : 'Stop (a simulation)'} disabled={!vm.canStop} onClick={() => void vm.stop()}>
        <Square aria-hidden="true" />
      </button>
    </>
  )
  const safeStopButton = (
    <button className="status-lost safe-stop" aria-label="Safe stop" title="Safe stop: the board stops the program and switches off everything" disabled={!vm.canSafeStop} onClick={() => void vm.safeStop()}>
      <OctagonX aria-hidden="true" />
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
    <WorkspaceShell className={`theme-${theme} ${panels.resizing ? 'is-resizing' : ''}`}>
      <ExplorerPanel open={panels.leftOpen} width={panels.left}>
        <div className="side-panel-header left-strip">
          {panels.leftOpen && <ViewTabs view={view} onView={setView} />}
          <button className="panel-toggle" aria-label={panels.leftOpen ? 'Collapse left panel' : 'Expand left panel'} onClick={() => panels.setLeftOpen(!panels.leftOpen)}>
            <PanelLeft aria-hidden="true" />
          </button>
        </div>
        {panels.leftOpen
          ? (
            <div className="side-panel-body">
              {view === 'Project'
                ? (
                  <>
                    <ProjectSummary name={project.project.name} unsaved={project.unsaved} counts={projectCounts(project.project)} />
                    <SettingsTabs sections={settingTabs} state={boardSettings} tab={projectTab} onTab={setProjectTab} />
                  </>
                )
                : view === 'Code'
                ? <VariablesExplorer code={code} />
                : view === 'Devices'
                ? <DevicesExplorer state={devices} />
                : view === 'Console'
                ? <ConsoleExplorer state={board} jumps={jumps} subject={consoleSubject} onSubject={setConsoleSubject} />
                : <div className="view-placeholder"><h2>{current.title}</h2><p>{current.note}</p></div>}
            </div>
          )
          : <div className="side-panel-icons"><ViewTabs view={view} onView={(next) => { setView(next); panels.setLeftOpen(true) }} /></div>}
      </ExplorerPanel>

      {panels.leftOpen && (
        <ResizeHandle side="left" label="Resize left panel" width={panels.left} onStart={() => panels.setResizing(true)} onEnd={() => panels.setResizing(false)} onResize={panels.dragLeft} />
      )}

      <div className="center-column">
        <MainScreen
          label="Main screen"
          toolbar={
            <>
              {screens && (
                // One button, a full touch target: it shows the screen it switches to.
                <button aria-label={mode === 'canvas' ? 'Show the details' : 'Show the canvas'} data-help="Switches between the lists and settings of this screen and its drawing." title={mode === 'canvas' ? 'Details: lists and settings' : 'Canvas: the drawing'} onClick={() => setMode(mode === 'canvas' ? 'details' : 'canvas')}>
                  {mode === 'canvas' ? <SlidersHorizontal aria-hidden="true" /> : <Workflow aria-hidden="true" />}
                </button>
              )}
              <button aria-label="Undo" data-help="Undoes the last change (Ctrl+Z)." title="Undo (Ctrl+Z)" disabled={!project.canUndo} onClick={project.undo}><Undo2 aria-hidden="true" /></button>
              <button aria-label="Redo" data-help="Redoes the change that was undone (Ctrl+Y)." title="Redo (Ctrl+Y)" disabled={!project.canRedo} onClick={project.redo}><Redo2 aria-hidden="true" /></button>
              {(view === 'Devices' || view === 'Project') && (
                <>
                  <button aria-label="Write to the board" data-help="Writes the project to the board: the settings it keeps, its devices, their start commands, the program and the actions." title={state === 'connected' ? (devices.pending ? 'Write to the board: there are changes to write (the devices, their start commands, the program and the actions)' : 'Write to the board: the devices, their start commands, the program and the actions (the stored project)') : 'Connect the board first'}
                    className={devices.pending ? 'toolbar-pending' : undefined} disabled={state !== 'connected' || devices.busy !== undefined} onClick={devices.write}>
                    <HardDriveUpload aria-hidden="true" />
                  </button>
                  <button aria-label="Read from the board" data-help="Reads the settings, devices, start commands and actions the board keeps into the project. Undo goes back." title={state === 'connected' ? 'Read from the board: the devices, their start commands and the actions it keeps (Undo goes back)' : 'Connect the board first'}
                    disabled={state !== 'connected' || devices.busy !== undefined} onClick={devices.read}>
                    <HardDriveDownload aria-hidden="true" />
                  </button>
                  {view === 'Devices' && (
                    <button className="toolbar-danger" aria-label="Remove the device from the project" data-help="Removes the selected device from the project. It leaves the board at the next write." title="Remove the selected device from the project (it leaves the board at the next write)"
                      disabled={!devices.selectedDevice || devices.selectedDevice.system} onClick={() => devices.selectedDevice && devices.removeDevice(devices.selectedDevice.id)}>
                      <Trash2 aria-hidden="true" />
                    </button>
                  )}
                </>
              )}
              <span className="toolbar-spacer" />
              {(view === 'Devices' || view === 'Project') && devices.busy && <span className="bar-note" role="status">{devices.busy.step}… {devices.busy.done}/{devices.busy.total}</span>}
              {(view === 'Devices' || view === 'Project') && !devices.busy && devices.note && <span className={`bar-note ${devices.note.tone === 'error' ? 'is-error' : ''}`} role="status" title={devices.note.text} onClick={devices.clearNote}>{devices.note.text}</span>}
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
            ? <ConsoleScreen state={board} jumps={jumps} show={consoleShow} onShow={setConsoleShow} subject={consoleSubject} onSubject={setConsoleSubject} />
            : view === 'Devices' && mode === 'details'
            ? <DevicesScreen state={devices} />
            : view === 'Code' && mode === 'details'
            ? <VariablesEditor code={code} />
            : screens
            ? <div className={`view-placeholder ${mode === 'canvas' ? 'canvas-empty' : ''}`}><h2>{current.title} · {mode === 'canvas' ? 'Canvas' : 'Details'}</h2><p>{screens[mode]}</p></div>
            : view === 'Project'
              ? (tabSection
                ? <SettingsScreen section={tabSection} state={boardSettings} context={pinContext} nameOf={nameOf} />
                : <ProjectScreen project={project} link={link} connected={state === 'connected'} run={vm.board} settings={boardSettings} context={pinContext} nameOf={nameOf} />)
              : <div className="view-placeholder"><h2>{current.title}</h2><p>Nothing here yet.</p></div>}
        </MainScreen>
      </div>

      {panels.rightOpen && (
        <ResizeHandle side="right" label="Resize right panel" width={panels.right} onStart={() => panels.setResizing(true)} onEnd={() => panels.setResizing(false)} onResize={panels.dragRight} />
      )}

      <DetailsPanel open={panels.rightOpen} style={panels.rightOpen ? { width: panels.right } : undefined}>
        <div className="side-panel-header right-header">
          {panels.rightOpen
            ? (
              <>
                <button className="panel-toggle" aria-label="Collapse right panel" onClick={closeDetails}>
                  <PanelRight aria-hidden="true" />
                </button>
                <div className="header-icons">{runButtons}{consoleButton}{linkButton}{vm.running && safeStopButton}</div>
              </>
            )
            : vm.running ? safeStopButton : linkButton}
        </div>
        {panels.rightOpen
          ? (
            <div className="side-panel-body">
              {!connectionOpen && testDevice && (devices.pinSelection?.device === testDevice.id
                ? <PinPanel key={`${testDevice.id}/${devices.pinSelection.pin}`} device={testDevice} pin={devices.pinSelection.pin} state={devices} />
                : <DeviceTestPanel key={testDevice.id} device={testDevice} state={devices} />)}
              {!connectionOpen && codeVariable !== undefined && <VariableDetails code={code} />}
              {connectionOpen && <ConnectionPanel state={state} device={device} reason={reason} available={available} scan={scan} onChoose={choose} onCancelScan={cancelScan} auto={auto} onAuto={setAuto} sync={autoSync} onSync={setAutoSync} onConnect={() => void connect()} onDisconnect={() => void disconnect()} onForget={() => void forget()} />}
            </div>
          )
          : <div className="side-panel-icons">{vm.running && linkButton}{runButtons}{consoleButton}</div>}
      </DetailsPanel>
      <HelpOverlay on={help} />
    </WorkspaceShell>
    </ConsoleContext.Provider>
  )
}
