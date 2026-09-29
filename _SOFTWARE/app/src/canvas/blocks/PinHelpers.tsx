import { useMemo } from 'react'
import { X } from 'lucide-react'
import { Badge } from '../../components/Badge'
import { TypeBadge } from '../../components/TypeBadge/TypeBadge'
import { newObjectId } from '../../domain/project'
import type { ObjectNode, ProjectDocument, ValueNode } from '../../domain/project'
import { AccessorEditor } from './AccessorEditor'
import { arrayDims, parseChain, resolveChain, rootCandidates, shapeText } from './accessorChain'
import { isValidVariableName, mapPinTypeToVmType, nextVarName, resolveUniversalNode } from './pinAccessors'
import '../Canvas.css'

/** Full-width helpers card rendered directly below the pins list when editing a pin. */
export function PinHelpersPanel({
  pinTitle,
  pinType,
  currentText,
  project,
  selectedObjectId,
  onClearSelectedObject,
  onCreateVariable,
  onApply,
  onClose,
}: {
  pinTitle: string
  pinType: string
  currentText: string
  project?: ProjectDocument
  selectedObjectId?: string
  onClearSelectedObject?: () => void
  onCreateVariable?: (node: ObjectNode) => void
  onApply: (text: string) => void
  onClose: () => void
}) {
  const candidates = useMemo(() => rootCandidates(project), [project])
  const chain = useMemo(() => parseChain(currentText, project), [currentText, project])
  const result = useMemo(() => (project && chain.root ? resolveChain(project, chain) : undefined), [project, chain])
  const base = chain.root

  const selectedCandidate = useMemo(() => {
    if (!selectedObjectId || !project) return undefined
    return resolveUniversalNode(selectedObjectId, project)
  }, [selectedObjectId, project])

  const filteredCandidates = useMemo(() => {
    const query = (base || currentText.trim()).toLowerCase()
    if (!query) return candidates
    return candidates.filter((c) => c.name.toLowerCase().includes(query) || c.fullPath.toLowerCase().includes(query))
  }, [candidates, base, currentText])
  // The list of variables is for choosing the root: once one is reached and steps follow, the chain editor takes over.
  const choosingRoot = currentText.trim().length > 0 && (!result?.node || chain.steps.length === 0)

  return (
    <div
      className="pin-helpers-card"
      onMouseDown={(e) => {
        const tag = (e.target as HTMLElement).tagName
        if (tag !== 'BUTTON' && tag !== 'INPUT') e.preventDefault()
      }}
    >
      <div className="pin-helpers-header">
        <div className="pin-helpers-heading">
          <Badge tone="accent" caps filled>Pin Helper</Badge>
          <strong>{pinTitle}</strong>
          <TypeBadge type={pinType} />
        </div>
        <button type="button" className="pin-helpers-close" onClick={onClose} title="Close helper">
          <X aria-hidden="true" />
        </button>
      </div>

      {selectedCandidate && (
        <div className="pin-helpers-warning" style={{ background: 'color-mix(in srgb, var(--accent) 16%, transparent)', borderColor: 'var(--accent)' }}>
          <span>Selected in tree: <strong>{selectedCandidate.name}</strong> ({selectedCandidate.fullPath}): click a [variable] slot to use it.</span>
        </div>
      )}

      <AccessorEditor text={currentText} project={project} selected={selectedCandidate} onApply={onApply} onClearSelected={onClearSelectedObject} />

      {choosingRoot && (
        <div className="pin-helpers-section">
          <div className="pin-helpers-label">
            <span>Variables ({filteredCandidates.length}):</span>
            {base && <Badge>filter: &quot;{base}&quot;</Badge>}
          </div>
          <div className="pin-helpers-var-list">
            {filteredCandidates.slice(0, 8).map((cand) => (
              <button key={cand.id} type="button" className="pin-helpers-var-row" onClick={() => onApply(`${cand.fullPath}${cand.suffix}`)}>
                <span className="pin-helpers-var-name">{cand.fullPath}</span>
                {cand.node.kind === 'value' && <TypeBadge type={cand.node.type} />}
                {(arrayDims(cand.node, project) || cand.node.kind === 'folder') && <span className="pin-helpers-var-count">{shapeText(cand.node, project)}</span>}
              </button>
            ))}
            {filteredCandidates.length === 0 && (
              <div className="pin-helpers-empty">No matching variables found</div>
            )}
          </div>
        </div>
      )}

      {/* Variable Creator: when typed variable does not exist yet */}
      {(() => {
        const raw = (base || currentText.trim()).replace(/[[\]'"]/g, '').trim()
        const hasMatch = candidates.some((c) => c.name.toLowerCase() === raw.toLowerCase() || c.fullPath.toLowerCase() === raw.toLowerCase())
        const canCreateTyped = raw.length > 0 && !hasMatch && isValidVariableName(raw) && !!onCreateVariable
        const vmType = mapPinTypeToVmType(pinType)

        return (
          <>
            {canCreateTyped && (
              <div className="pin-helpers-creator-bar">
                <span className="pin-helpers-creator-msg">
                  Variable <strong>&quot;{raw}&quot;</strong> does not exist yet.
                </span>
                <button
                  type="button"
                  className="pin-helpers-create-btn"
                  onClick={() => {
                    const newNode: ValueNode = {
                      kind: 'value',
                      id: newObjectId(),
                      name: raw,
                      type: vmType,
                      length: 1,
                      mutable: true,
                      retentive: false,
                      typeMode: 'auto',
                    }
                    onCreateVariable(newNode)
                    onApply(raw)
                  }}
                >
                  + Create &quot;{raw}&quot; [{vmType}]
                </button>
              </div>
            )}
            {onCreateVariable && !canCreateTyped && (
              <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: '4px' }}>
                <button
                  type="button"
                  className="pin-helpers-new-var-btn"
                  title={`Create a new ${vmType} variable in project`}
                  onClick={() => {
                    const siblings = project?.objects ?? []
                    const prefix = (pinTitle.toLowerCase().replace(/[^a-z0-9_]/g, '') || 'var')
                    const name = nextVarName(siblings, prefix)
                    const newNode: ValueNode = {
                      kind: 'value',
                      id: newObjectId(),
                      name,
                      type: vmType,
                      length: 1,
                      mutable: true,
                      retentive: false,
                      typeMode: 'auto',
                    }
                    onCreateVariable(newNode)
                    onApply(name)
                  }}
                >
                  + New {vmType} Variable
                </button>
              </div>
            )}
          </>
        )
      })()}

      <div className="pin-helpers-tips">
        <div><strong>Steps:</strong></div>
        <div>• <code>.name</code> / <code>[&quot;key&quot;]</code> : a member of a folder or object, by name.</div>
        <div>• <code>[3]</code> : a fixed position; <code>[sel]</code> : a position read from a variable (drop it, or select it in the tree and click).</div>
        <div>• <code>cube[plane][row][col]</code> : arrays of two or three dimensions take one step per dimension.</div>
      </div>
    </div>
  )
}
