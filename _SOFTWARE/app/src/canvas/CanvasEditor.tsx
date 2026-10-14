import type { ProjectDocument } from '../domain/project'
import { CanvasSurface } from './CanvasSurface'
import { CanvasTabs } from './CanvasTabs'
import type { CanvasWorkspace } from './useCanvasWorkspace'
import './Canvas.css'

/** The canvas mode: the open canvas, and the canvas list along the bottom. */
export function CanvasEditor({ workspace, showGrid, project, onBackgroundClick }: { workspace: CanvasWorkspace; showGrid: boolean; project?: ProjectDocument; onBackgroundClick?: () => void }) {
  return (
    <div className="canvas-editor">
      <CanvasSurface workspace={workspace} showGrid={showGrid} project={project} onBackgroundClick={onBackgroundClick} />
      <CanvasTabs workspace={workspace} />
    </div>
  )
}
