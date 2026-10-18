import React from 'react'
import { ChevronDown, Trash2 } from 'lucide-react'
import './TreeSlab.css'

export interface TreeSlabProps {
  /** The primary icon / designator displayed on the left (adjusted like in data connectors) */
  icon?: React.ReactNode
  /** The main text / label */
  label: React.ReactNode
  /** An editor in place of the label (inline rename). The row's button becomes a plain box meanwhile: an input does not belong inside a button. */
  labelEditor?: React.ReactNode
  /** Tooltip title for label or entire slab */
  title?: string
  /** Right-aligned badges / chips / icons: "n x icon (badge)" */
  badges?: React.ReactNode
  /** A reference pill (RefPill compact) after the badges: what a reference row points at. */
  refSlot?: React.ReactNode
  /** Optional disclosure button/chevron */
  disclosure?: React.ReactNode
  /** Position of disclosure chevron if present. Defaults to 'right' (matching Icon - text - n x icon layout) */
  disclosurePosition?: 'right' | 'left'
  /** Trailing action buttons (e.g. delete trash button) */
  actions?: React.ReactNode
  /** Prevent selection or activation (for unavailable palette entries). */
  disabled?: boolean
  /** Optional semantic role for flat palettes. */
  role?: React.AriaRole
  /** Selection state */
  selected?: boolean
  /** Folder style variant */
  isFolder?: boolean
  /** Reference style variant */
  isReference?: boolean
  /** Search match highlight */
  isMatch?: boolean
  /** An error is about this row (error.css): red edge and fill. */
  isError?: boolean
  /** The key of what the row stands for (`refKey`, BACKEND.md): what an error's ref marks. */
  refKey?: string
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
  onClick?: (e: React.MouseEvent<HTMLDivElement>) => void
  onDoubleClick?: (e: React.MouseEvent<HTMLDivElement>) => void
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
  labelEditor,
  title,
  badges,
  refSlot,
  disclosure,
  disclosurePosition = 'right',
  actions,
  disabled = false,
  role,
  selected = false,
  isFolder = false,
  isReference = false,
  isMatch = false,
  isError = false,
  refKey,
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
  const hasBadges = Boolean(badges)
  const computedTitle = title || (typeof label === 'string' ? label : undefined)

  const rowClasses = [
    'tree-slab-row',
    hasBadges ? 'has-badges' : '',
    disabled ? 'is-disabled' : '',
    twoRowOnNarrow ? 'two-row-narrow' : '',
    twoRow ? 'two-row' : '',
    selected ? 'selected' : '',
    isFolder ? 'folder' : '',
    isReference ? 'reference' : '',
    isMatch ? 'is-search-match' : '',
    isError ? 'is-error' : '',
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
      data-key={refKey}
      role={role}
      title={computedTitle}
      draggable={disabled ? false : draggable}
      onDragStart={onDragStart}
      onDragEnd={onDragEnd}
      onDragOver={onDragOver}
      onDragLeave={onDragLeave}
      onDrop={onDrop}
      onClick={(e) => {
        if (disabled) return
        if ((e.target as HTMLElement).closest('.tree-slab-disclosure, .tree-slab-action, .tree-slab-item')) {
          return
        }
        onClick?.(e as unknown as React.MouseEvent<HTMLDivElement>)
      }}
    >
      {disclosurePosition === 'left' && disclosure}
      {(() => {
        const inner = children ? (
          children
        ) : (
          <>
            {icon && <span className="tree-slab-icon">{icon}</span>}
            <div className="tree-slab-content">
              <div className="tree-slab-header">
                <span className="tree-slab-label" title={computedTitle}>
                  {labelEditor ?? label}
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
              {refSlot && <span className="tree-slab-ref">{refSlot}</span>}
            </div>
          </>
        )
        return labelEditor ? (
          <div className={`tree-slab-item is-editing ${itemClassName}`.trim()}>{inner}</div>
        ) : (
          // Not a <button>: the row can hold a disclosure button, and a button cannot sit inside a button.
          <div
            role="button"
            tabIndex={disabled ? -1 : 0}
            className={`tree-slab-item ${itemClassName}`.trim()}
            aria-label={ariaLabel || computedTitle}
            aria-current={selected ? 'true' : undefined}
            aria-disabled={disabled || undefined}
            onClick={disabled ? undefined : onClick}
            onDoubleClick={onDoubleClick}
            onKeyDown={(event) => {
              if (disabled || event.target !== event.currentTarget || (event.key !== 'Enter' && event.key !== ' ')) return
              event.preventDefault()
              onClick?.(event as unknown as React.MouseEvent<HTMLDivElement>)
            }}
          >
            {inner}
          </div>
        )
      })()}
      {actions}
    </div>
  )
}

/** The chevron that opens and closes a folder row (TreeSlab `disclosure`). */
export function TreeDisclosure({ open, onToggle }: { open: boolean; onToggle: () => void }) {
  return (
    <button type="button" className="tree-slab-disclosure" aria-expanded={open} aria-label={open ? 'Collapse' : 'Expand'} onClick={(event) => { event.stopPropagation(); onToggle() }}>
      <ChevronDown aria-hidden="true" />
    </button>
  )
}

/** A trailing row action (TreeSlab `actions`); the delete one by default. */
export function TreeAction({ label = 'Delete', icon: Icon = Trash2, onClick }: { label?: string; icon?: typeof Trash2; onClick: () => void }) {
  return (
    <button type="button" className="tree-slab-action" title={label} aria-label={label} onClick={(event) => { event.stopPropagation(); onClick() }}>
      <Icon aria-hidden="true" />
    </button>
  )
}

/** The rows under a folder: indented, with a guide line; nothing rendered while closed. */
export function TreeChildren({ open, children }: { open: boolean; children?: React.ReactNode }) {
  return <div className="tree-children" hidden={!open}>{open && children}</div>
}
