import type { ButtonHTMLAttributes, HTMLAttributes } from 'react'
import './OptionMenu.css'

interface OptionMenuProps extends HTMLAttributes<HTMLDivElement> {
  onCancel?: () => void
}

/** Shared listbox for choices that need richer rows than a native select. */
export function OptionMenu({ children, className = '', onCancel, onKeyDown, ...props }: OptionMenuProps) {
  return <div {...props} role="listbox" className={`option-menu ${className}`.trim()} onKeyDown={(event) => {
    if (event.key === 'Escape' && onCancel) { event.preventDefault(); onCancel() }
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      const options = [...event.currentTarget.querySelectorAll<HTMLButtonElement>('button[role="option"]:not(:disabled)')]
      if (options.length) {
        event.preventDefault()
        const current = options.indexOf(document.activeElement as HTMLButtonElement)
        const next = event.key === 'ArrowDown' ? (current + 1) % options.length : (current - 1 + options.length) % options.length
        options[next]?.focus()
      }
    }
    onKeyDown?.(event)
  }}>{children}</div>
}

interface MenuOptionProps extends Omit<ButtonHTMLAttributes<HTMLButtonElement>, 'onSelect'> {
  selected?: boolean
  keepInputFocus?: boolean
  onChoose: () => void
}

export function MenuOption({ children, className = '', selected = false, keepInputFocus = false, onChoose, onClick, onMouseDown, ...props }: MenuOptionProps) {
  return <button {...props} type="button" role="option" aria-selected={selected} className={`option-menu-option ${className}`.trim()}
    onMouseDown={(event) => { if (keepInputFocus) event.preventDefault(); onMouseDown?.(event) }}
    onClick={(event) => { event.stopPropagation(); onChoose(); onClick?.(event) }}>
    {children}
  </button>
}
