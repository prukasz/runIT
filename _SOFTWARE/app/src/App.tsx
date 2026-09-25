import { useRef, useState } from 'react'
import { ArrowLeftRight, Box, Bug, ChevronLeft, ChevronRight, Code2, Cpu, Gamepad2, Grid2X2, Moon, PanelLeftClose, PanelLeftOpen, Play, Plug, Redo2, Settings2, Square, SquareTerminal, Sun, Undo2, X } from 'lucide-react'
import './App.css'

const views = [
  { name: 'Board', icon: Cpu },
  { name: 'Devices', icon: Box },
  { name: 'Code', icon: Code2 },
  { name: 'Remote', icon: Gamepad2 },
  { name: 'Settings', icon: Settings2 },
]

const details = [
  { name: 'Run', icon: Play },
  { name: 'Stop', icon: Square },
  { name: 'Connect', icon: Plug },
  { name: 'Debug', icon: Bug },
]

const settingsGroups = ['Settings group A', 'Settings group B', 'Settings group C']
const codePalettes = ['Blocks', 'Variables'] as const
type CodePalette = typeof codePalettes[number]
type CodeMode = 'manage' | 'canvas'

export default function App() {
  const [view, setView] = useState('Code')
  const [settingsGroup, setSettingsGroup] = useState(settingsGroups[0])
  const [codePalette, setCodePalette] = useState<CodePalette>('Blocks')
  const [codeMode, setCodeMode] = useState<CodeMode>('manage')
  const [showCanvasGrid, setShowCanvasGrid] = useState(true)
  const [leftOpen, setLeftOpen] = useState(true)
  const [leftWidth, setLeftWidth] = useState(260)
  const [detail, setDetail] = useState('Run')
  const [rightOpen, setRightOpen] = useState(false)
  const [rightWidth, setRightWidth] = useState(320)
  const [terminalOpen, setTerminalOpen] = useState(false)
  const [terminalHeight, setTerminalHeight] = useState(220)
  const [resizing, setResizing] = useState<'left' | 'right' | 'bottom' | null>(null)
  const [theme, setTheme] = useState<'dark' | 'light'>(() => window.localStorage.getItem('runit-shell-theme') === 'light' ? 'light' : 'dark')
  const resizeActive = useRef(false)
  const leftResizeActive = useRef(false)
  const bottomResizeActive = useRef(false)

  const resizeLeft = (clientX: number) => {
    const maximum = Math.max(180, window.innerWidth - (rightOpen ? rightWidth : 54) - 180)
    const minimum = Math.min(260, maximum)
    setLeftWidth(Math.min(maximum, Math.max(minimum, clientX)))
  }

  const resizeRight = (clientX: number) => {
    const maximum = Math.max(180, window.innerWidth - 180)
    const minimum = Math.min(260, maximum)
    setRightWidth(Math.min(maximum, Math.max(minimum, window.innerWidth - clientX)))
  }

  const resizeBottom = (clientY: number) => {
    const maximum = Math.max(120, window.innerHeight - 160)
    setTerminalHeight(Math.min(maximum, Math.max(120, window.innerHeight - clientY)))
  }

  const toggleTheme = () => {
    const next = theme === 'dark' ? 'light' : 'dark'
    setTheme(next)
    window.localStorage.setItem('runit-shell-theme', next)
  }

  return (
    <main className={`shell theme-${theme} ${resizing ? `is-resizing-${resizing}` : ''}`}>
      <aside className={`left-panel ${leftOpen ? 'is-open' : ''}`} style={leftOpen ? { width: leftWidth } : undefined} aria-label="Explorer">
        {leftOpen && <div className="left-resize-handle" role="separator" aria-label="Resize explorer panel" aria-orientation="vertical" tabIndex={0} onPointerDown={(event) => { leftResizeActive.current = true; setResizing('left'); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={(event) => { if (leftResizeActive.current) resizeLeft(event.clientX) }} onPointerUp={() => { leftResizeActive.current = false; setResizing(null) }} onPointerCancel={() => { leftResizeActive.current = false; setResizing(null) }} onKeyDown={(event) => { if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); resizeLeft(leftWidth + (event.key === 'ArrowLeft' ? -16 : 16)) } }} />}
        <div className="left-strip">
          <div className="view-menu" role="tablist" aria-label="Current view">
            {views.map(({ name, icon: Icon }) => (
              <button key={name} role="tab" aria-selected={view === name} title={name} className={view === name ? 'selected' : ''} onClick={() => setView(name)}><Icon aria-hidden="true" /></button>
            ))}
          </div>
          <button className={`terminal-launch ${terminalOpen ? 'selected' : ''}`} aria-label={terminalOpen ? 'Hide terminal' : 'Show terminal'} title="Terminal" aria-pressed={terminalOpen} onClick={() => setTerminalOpen(!terminalOpen)}><SquareTerminal aria-hidden="true" /></button>
          <button className="panel-toggle" aria-label={leftOpen ? 'Collapse explorer' : 'Expand explorer'} onClick={() => setLeftOpen(!leftOpen)}>{leftOpen ? <PanelLeftClose /> : <PanelLeftOpen />}</button>
        </div>
        {leftOpen && <div className="left-content" aria-label={`${view} explorer panel`}>
          {view === 'Settings' && <nav className="settings-groups" aria-label="Settings groups">
            {settingsGroups.map((group) => <button key={group} className={`settings-group-button ${settingsGroup === group ? 'selected' : ''}`} aria-current={settingsGroup === group ? 'page' : undefined} onClick={() => setSettingsGroup(group)}>{group}</button>)}
          </nav>}
          {view === 'Code' && <div className="code-palette">
            <div className="code-palette-tabs" role="tablist" aria-label="Code palettes">
              {codePalettes.map((palette) => <button key={palette} role="tab" aria-selected={codePalette === palette} className={codePalette === palette ? 'selected' : ''} onClick={() => { setCodePalette(palette); setCodeMode('manage') }}>{palette}</button>)}
            </div>
            <div className="code-palette-body" aria-label={`${codePalette} palette`} />
            <div className="code-mode-footer">
              <button className="code-mode-toggle" onClick={() => setCodeMode(codeMode === 'manage' ? 'canvas' : 'manage')}><ArrowLeftRight aria-hidden="true" /><span>Switch to {codeMode === 'manage' ? 'Canvas' : 'View / Manage'}</span></button>
            </div>
          </div>}
          {view === 'Remote' && <div className="remote-palette" aria-label="Gamepad controls palette">
            <div className="remote-palette-title">Controls</div>
            <div className="remote-palette-body" />
          </div>}
        </div>}
      </aside>

      <div className="center-column">
      <section className="main-panel" aria-label={view === 'Settings' ? `${settingsGroup} screen` : view === 'Code' ? `${codePalette} ${codeMode} screen` : `${view} main panel`}>
        <div className="main-action-bar" role="toolbar" aria-label="Screen actions">
          <div className="history-actions">
            <button aria-label="Undo" title="Undo" disabled><Undo2 aria-hidden="true" /></button>
            <button aria-label="Redo" title="Redo" disabled><Redo2 aria-hidden="true" /></button>
          </div>
          <div className="screen-actions" aria-label={`${view} specific actions`}>
            {view === 'Code' && codeMode === 'canvas' && <button aria-label={showCanvasGrid ? 'Hide canvas grid' : 'Show canvas grid'} title={showCanvasGrid ? 'Hide canvas grid' : 'Show canvas grid'} aria-pressed={showCanvasGrid} className={showCanvasGrid ? 'selected' : ''} onClick={() => setShowCanvasGrid(!showCanvasGrid)}><Grid2X2 aria-hidden="true" /></button>}
          </div>
          <button className="theme-toggle" aria-label={`Switch to ${theme === 'dark' ? 'light' : 'dark'} mode`} title={`Switch to ${theme === 'dark' ? 'light' : 'dark'} mode`} onClick={toggleTheme}>{theme === 'dark' ? <Sun /> : <Moon />}</button>
        </div>
        <div className={`main-surface ${view === 'Code' && codeMode === 'canvas' && showCanvasGrid ? 'code-canvas' : ''}`} />
      </section>
      {terminalOpen && <section className="terminal-panel" style={{ height: terminalHeight }} aria-label="Terminal panel">
        <div className="bottom-resize-handle" role="separator" aria-label="Resize terminal panel" aria-orientation="horizontal" tabIndex={0} onPointerDown={(event) => { bottomResizeActive.current = true; setResizing('bottom'); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={(event) => { if (bottomResizeActive.current) resizeBottom(event.clientY) }} onPointerUp={() => { bottomResizeActive.current = false; setResizing(null) }} onPointerCancel={() => { bottomResizeActive.current = false; setResizing(null) }} onKeyDown={(event) => { if (event.key === 'ArrowUp' || event.key === 'ArrowDown') { event.preventDefault(); resizeBottom(window.innerHeight - terminalHeight + (event.key === 'ArrowUp' ? -16 : 16)) } }} />
        <div className="terminal-header"><span>Terminal</span><button aria-label="Close terminal" title="Close terminal" onClick={() => setTerminalOpen(false)}><X aria-hidden="true" /></button></div>
        <div className="terminal-body" />
      </section>}
      </div>

      <aside className={`right-panel ${rightOpen ? 'is-open' : ''}`} style={rightOpen ? { width: rightWidth } : undefined} aria-label="Details panel">
        {rightOpen && <div className="resize-handle" role="separator" aria-label="Resize details panel" aria-orientation="vertical" tabIndex={0} onPointerDown={(event) => { resizeActive.current = true; setResizing('right'); event.currentTarget.setPointerCapture(event.pointerId) }} onPointerMove={(event) => { if (resizeActive.current) resizeRight(event.clientX) }} onPointerUp={() => { resizeActive.current = false; setResizing(null) }} onPointerCancel={() => { resizeActive.current = false; setResizing(null) }} onKeyDown={(event) => { if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') { event.preventDefault(); resizeRight(window.innerWidth - rightWidth + (event.key === 'ArrowLeft' ? -16 : 16)) } }} />}
        <div className="detail-menu" role="tablist" aria-label="Detail sections">
          {details.map(({ name, icon: Icon }) => (
            <button key={name} role="tab" aria-selected={detail === name} title={name} className={detail === name ? 'selected' : ''} onClick={() => setDetail(name)}><Icon aria-hidden="true" /></button>
          ))}
          <button className="detail-toggle" aria-label={rightOpen ? 'Collapse details' : 'Expand details'} onClick={() => setRightOpen(!rightOpen)}>{rightOpen ? <ChevronRight /> : <ChevronLeft />}</button>
        </div>
        {rightOpen && <div className="detail-content" aria-label={`${detail} content panel`} />}
      </aside>
    </main>
  )
}
