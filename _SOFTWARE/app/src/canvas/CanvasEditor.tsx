import type { DeviceCatalog } from '../domain/descriptors'
import type { Diagnostic } from '../domain/compiler'
import type { ProjectDevice, ProjectDocument } from '../domain/project'
import { CanvasSurface } from './CanvasSurface'
import { CanvasTabs } from './CanvasTabs'
import type { CanvasWorkspace } from './useCanvasWorkspace'
import './Canvas.css'

/** The Code view's canvas mode: the open canvas, and the canvas list along the bottom. */
export function CanvasEditor({
  workspace,
  showGrid,
  diagnostics = new Map(),
  devices = [],
  deviceCatalog,
  project,
  selectedObjectId,
  onClearSelectedObject,
}: {
  workspace: CanvasWorkspace
  showGrid: boolean
  diagnostics?: ReadonlyMap<string, readonly Diagnostic[]>
  devices?: readonly ProjectDevice[]
  deviceCatalog?: DeviceCatalog
  project?: ProjectDocument
  selectedObjectId?: string
  onClearSelectedObject?: () => void
}) {
  return (
    <div className="canvas-editor">
      <CanvasSurface
        workspace={workspace}
        showGrid={showGrid}
        diagnostics={diagnostics}
        devices={devices}
        deviceCatalog={deviceCatalog}
        project={project}
        selectedObjectId={selectedObjectId}
        onClearSelectedObject={onClearSelectedObject}
      />
      <CanvasTabs workspace={workspace} />
    </div>
  )
}
