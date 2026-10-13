import { useEffect, useMemo, useRef, useState } from 'react'
import type { PointerEvent, KeyboardEvent } from 'react'
import { Bluetooth, BluetoothConnected, BluetoothOff, LayoutGrid, Moon, PanelLeft, PanelRight, Play, Redo2, Rows3, Square, Sun, Undo2 } from 'lucide-react'
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
  const [rightOpen, setRightOpen] = useState(true)
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
  const [mode, setMode] = usePersistedChoice('runit.code.mode', ['manage', 'canvas'] as const, 'manage')
  // The right panel follows the selection: the block picked on the canvas, else the object picked in the tree.
  const selectedBlock = mode === 'canvas' && canvas.selectedIds.length === 1 ? findBlock(canvas.canvases, canvas.selectedIds[0]!)?.block : undefined
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
          <button className="panel-toggle" aria-label={leftOpen ? 'Collapse left panel' : 'Expand left panel'} onClick={() => setLeftOpen(!leftOpen)}>
            <PanelLeft aria-hidden="true" />
          </button>
        </div>
        {leftOpen
          ? (
            <div className="side-panel-body">
              <div className="palette-switch"><SegmentedSwitch label="Palette" value={palette} options={PALETTES} onChange={setPalette} /></div>
              {palette === 'Variables' ? <ObjectTreePalette workspace={objects} /> : <BlockPalette selected={pick} onSelect={setPick} onDragType={(type) => { canvas.paletteDrag.current = type }} />}
            </div>
          )
          : <div className="side-panel-icons" />}
      </ExplorerPanel>

      <div className="center-column">
        <MainScreen
          label="Main screen"
          toolbar={
            <>
              <button aria-label={mode === 'canvas' ? 'Show the objects and blocks' : 'Show the canvas'} title={mode === 'canvas' ? 'Objects and blocks' : 'Canvas'} className={mode === 'canvas' ? 'selected' : ''} onClick={() => setMode(mode === 'canvas' ? 'manage' : 'canvas')}>
                {mode === 'canvas' ? <Rows3 aria-hidden="true" /> : <LayoutGrid aria-hidden="true" />}
              </button>
              <button aria-label="Undo" title="Undo (Ctrl+Z)" disabled={mode === 'canvas' ? !canvas.canUndo : !objects.canUndo} onClick={mode === 'canvas' ? canvas.undo : objects.undo}><Undo2 aria-hidden="true" /></button>
              <button aria-label="Redo" title="Redo (Ctrl+Y)" disabled={mode === 'canvas' ? !canvas.canRedo : !objects.canRedo} onClick={mode === 'canvas' ? canvas.redo : objects.redo}><Redo2 aria-hidden="true" /></button>
              <span className="toolbar-spacer" />
              <button aria-label={theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme'} title={theme === 'dark' ? 'Light theme' : 'Dark theme'} onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}>
                {theme === 'dark' ? <Sun aria-hidden="true" /> : <Moon aria-hidden="true" />}
              </button>
            </>
          }
        >
          {mode === 'canvas' ? <CanvasEditor workspace={canvas} showGrid project={objects.project} /> : palette === 'Variables' ? <ObjectTreeEditor workspace={objects} /> : <BlockInfo placement={pick} />}
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
          ? <div className="side-panel-body">{bleOpen ? <BlePanel link={link} state={linkState} device={device} auto={auto} onAuto={setAuto} /> : selectedBlock ? <BlockDetails key={selectedBlock.id} block={selectedBlock} blocks={canvas.active?.blocks ?? []} descriptor={descriptorOf(selectedBlock.type)} project={objects.project} selectedObjectId={objects.selectedId ?? undefined} onClearSelectedObject={() => objects.select(null)} onCreateVariable={(node) => objects.edit((current) => addObject(current, null, node))} onChange={(change) => canvas.updateBlock(selectedBlock.id, change)} onDelete={() => canvas.deleteBlocks([selectedBlock.id])} /> : <ObjectDetails workspace={objects} />}</div>
          : <div className="side-panel-icons">{rightButtons}</div>}
      </DetailsPanel>
    </WorkspaceShell>
  )
}
