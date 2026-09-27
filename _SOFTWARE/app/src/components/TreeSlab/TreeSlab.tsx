import React from 'react'
import './TreeSlab.css'

export interface TreeSlabProps {
  /** The primary icon / designator displayed on the left (adjusted like in data connectors) */
  icon?: React.ReactNode
  /** The main text / label */
  label: React.ReactNode
  /** Tooltip title for label or entire slab */
  title?: string
  /** Right-aligned badges / chips / icons: "n x icon (badge)" */
  badges?: React.ReactNode
  /** Optional disclosure button/chevron */
  disclosure?: React.ReactNode
  /** Position of disclosure chevron if present. Defaults to 'right' (matching Icon - text - n x icon layout) */
  disclosurePosition?: 'right' | 'left'
  /** Trailing action buttons (e.g. delete trash button) */
  actions?: React.ReactNode
  /** Selection state */
  selected?: boolean
  /** Folder style variant */
  isFolder?: boolean
  /** Reference style variant */
  isReference?: boolean
  /** Search match highlight */
  isMatch?: boolean
  /** Drag-and-drop state flags */
  isDragging?: boolean
  isDropInto?: boolean
  isDropBefore?: boolean
  isDropAfter?: boolean
  isLinkTarget?: boolean
  isLinkDisabled?: boolean
  /** Overlap badges to second row when container is narrow (defaults to true) */
  twoRowOnNarrow?: boolean
  /** Force 2-row layout regardless of width */
  twoRow?: boolean
  /** Event handlers */
  onClick?: (e: React.MouseEvent<HTMLButtonElement>) => void
  onDoubleClick?: (e: React.MouseEvent<HTMLButtonElement>) => void
  draggable?: boolean
  onDragStart?: (e: React.DragEvent<HTMLDivElement>) => void
  onDragEnd?: (e: React.DragEvent<HTMLDivElement>) => void
  onDragOver?: (e: React.DragEvent<HTMLDivElement>) => void
  onDragLeave?: (e: React.DragEvent<HTMLDivElement>) => void
  onDrop?: (e: React.DragEvent<HTMLDivElement>) => void
  /** Additional class names */
  className?: string
  itemClassName?: string
  /** Accessible aria-label */
  ariaLabel?: string
  /** Children if custom interior content is needed */
  children?: React.ReactNode
}

export const TreeSlab: React.FC<TreeSlabProps> = ({
  icon,
  label,
  title,
  badges,
  disclosure,
  disclosurePosition = 'right',
  actions,
  selected = false,
  isFolder = false,
  isReference = false,
  isMatch = false,
  isDragging = false,
  isDropInto = false,
  isDropBefore = false,
  isDropAfter = false,
  isLinkTarget = false,
  isLinkDisabled = false,
  twoRowOnNarrow = false,
  twoRow = false,
  onClick,
  onDoubleClick,
  draggable,
  onDragStart,
  onDragEnd,
  onDragOver,
  onDragLeave,
  onDrop,
  className = '',
  itemClassName = '',
  ariaLabel,
  children,
}) => {
  const hasBadges = Boolean(badges) || Boolean(actions)
  const computedTitle = title || (typeof label === 'string' ? label : undefined)

  const rowClasses = [
    'tree-slab-row',
    'object-tree-row',
    hasBadges ? 'has-badges' : '',
    twoRowOnNarrow ? 'two-row-narrow' : '',
    twoRow ? 'two-row' : '',
    selected ? 'selected' : '',
    isFolder ? 'folder' : '',
    isReference ? 'reference' : '',
    isMatch ? 'is-search-match' : '',
    isLinkTarget ? 'link-target' : '',
    isLinkDisabled ? 'link-disabled' : '',
    isDragging ? 'is-dragging' : '',
    isDropInto ? 'drop-into' : '',
    isDropBefore ? 'drop-before' : '',
    isDropAfter ? 'drop-after' : '',
    className,
  ]
    .filter(Boolean)
    .join(' ')

  return (
    <div
      className={rowClasses}
      draggable={draggable}
      onDragStart={onDragStart}
      onDragEnd={onDragEnd}
      onDragOver={onDragOver}
      onDragLeave={onDragLeave}
      onDrop={onDrop}
      onClick={(e) => {
        if ((e.target as HTMLElement).closest('.tree-slab-disclosure, .tree-slab-action, .tree-slab-item')) {
          return
        }
        onClick?.(e as unknown as React.MouseEvent<HTMLButtonElement>)
      }}
    >
      {disclosurePosition === 'left' && disclosure}
      <button
        type="button"
        className={`tree-slab-item object-tree-item ${itemClassName}`.trim()}
        aria-label={ariaLabel || computedTitle}
        aria-current={selected ? 'true' : undefined}
        onClick={onClick}
        onDoubleClick={onDoubleClick}
      >
        {children ? (
          children
        ) : (
          <>
            {icon && <span className="tree-slab-icon">{icon}</span>}
            <div className="tree-slab-content">
              <div className="tree-slab-header">
                <span className="tree-slab-label object-tree-name" title={computedTitle}>
                  {label}
                </span>
                {disclosurePosition === 'right' && disclosure && (
                  <span className="tree-slab-disclosure-slot">{disclosure}</span>
                )}
              </div>
              {hasBadges && (
                <div className="tree-slab-badges">
                  {badges}
                </div>
              )}
            </div>
          </>
        )}
      </button>
      {actions}
    </div>
  )
}
