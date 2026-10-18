import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Check, ChevronDown, Lock } from 'lucide-react'
import { Badge } from '../Badge/Badge'
import { MenuOption, OptionMenu } from '../OptionMenu/OptionMenu'
import './ListableField.css'

export interface ListableOptionObject {
  value: string
  label: string
  disabled?: boolean
  /** Hover text: what the option means. */
  title?: string
  /** Options sharing a group are listed under its heading (the first option of a group places it). */
  group?: string
  /** Muted text at the row's end (why it is disabled, a unit). */
  hint?: string
  /** A warning badge at the row's end (`in use`). */
  badge?: string
}

export type ListableOption = string | ListableOptionObject

export interface ListableFieldProps {
  value: string
  onChange: (value: string) => void
  options: readonly ListableOption[]
  /** Shown while no option has `value`. */
  placeholder?: string
  /** Locked: the lock icon, no menu. */
  disabled?: boolean
  selected?: boolean
  /** The field's value is wrong (error.css). */
  error?: boolean
  iconTitle?: string
  showIcon?: boolean
  id?: string
  title?: string
  'aria-label'?: string
  className?: string
  /** Size overrides (a number is px, a string any CSS length). Unset ones follow the surrounding `--field-*` variables. */
  width?: number | string
  minWidth?: number | string
  height?: number | string
  fontSize?: number | string
  fontWeight?: number | string
  padding?: number | string
  radius?: number | string
}

const asObject = (option: ListableOption): ListableOptionObject => (typeof option === 'string' ? { value: option, label: option } : option)

/**
 * The one dropdown of the app. The trigger has the field's look; the list is the app's OptionMenu, not the browser's popup (which
 * it draws itself, light and unstyleable). Groups get a heading, an option can carry a hint and a badge, the menu opens upwards when
 * there is no room below. Keyboard: Enter / Space opens, arrows move, Escape closes.
 */
export function ListableField({
  value, onChange, options, placeholder = 'Choose…', disabled = false, selected = false, error = false, iconTitle, showIcon = true,
  id, title, 'aria-label': ariaLabel, className = '', width, minWidth, height, fontSize, fontWeight, padding, radius,
}: ListableFieldProps) {
  const [open, setOpen] = useState(false)
  const [up, setUp] = useState(false)
  const root = useRef<HTMLDivElement>(null)
  const trigger = useRef<HTMLButtonElement>(null)
  const menu = useRef<HTMLDivElement>(null)
  const items = options.map(asObject)
  const current = items.find((item) => item.value === value)

  const close = (focus = true) => {
    setOpen(false)
    if (focus) trigger.current?.focus()
  }

  useEffect(() => {
    if (!open) return
    const outside = (event: MouseEvent) => { if (!root.current?.contains(event.target as Node)) close(false) }
    document.addEventListener('mousedown', outside, true)
    return () => document.removeEventListener('mousedown', outside, true)
  }, [open])

  // Opened: upwards when the list does not fit below, and the chosen option (or the first) focused.
  useLayoutEffect(() => {
    if (!open || !root.current || !menu.current) return
    const box = root.current.getBoundingClientRect()
    const height = menu.current.offsetHeight
    setUp(window.innerHeight - box.bottom < height + 12 && box.top > height)
    const focus = menu.current.querySelector<HTMLButtonElement>('[aria-selected="true"]:not(:disabled)') ?? menu.current.querySelector<HTMLButtonElement>('button:not(:disabled)')
    focus?.focus()
  }, [open])

  const length = (size: number | string | undefined) => (typeof size === 'number' ? `${size}px` : size)
  const style = {
    ...(width !== undefined ? { width: length(width), flex: 'none' } : {}),
    ...(minWidth !== undefined ? { '--listable-min-width': length(minWidth) } : {}),
    ...(height !== undefined ? { '--field-height': length(height) } : {}),
    ...(fontSize !== undefined ? { '--field-font-size': length(fontSize) } : {}),
    ...(fontWeight !== undefined ? { '--listable-font-weight': String(fontWeight) } : {}),
    ...(padding !== undefined ? { '--field-padding': length(padding) } : {}),
    ...(radius !== undefined ? { '--field-radius': length(radius) } : {}),
  } as CSSProperties

  const status = disabled ? 'is-blocked' : selected || open ? 'is-selected' : 'is-editable'
  let group: string | undefined
  return (
    <div ref={root} className={`adorned-field-wrap listable-field-wrap dd ${status}${open ? ' is-open' : ''}${error ? ' is-error' : ''} ${className}`.trim()} style={style}>
      <button ref={trigger} type="button" id={id} title={title} aria-label={ariaLabel} disabled={disabled} aria-haspopup="listbox" aria-expanded={open}
        className="form-field-control form-select-control listable-field-select dd-trigger" onClick={() => (open ? close() : setOpen(true))}>
        {current ? `${current.group ? `${current.group} · ` : ''}${current.label}` : <span className="dd-placeholder">{placeholder}</span>}
      </button>
      {showIcon && (
        <span className="field-adornment-slot" title={iconTitle ?? (disabled ? 'Locked' : 'Choose')}>
          {disabled ? <Lock className="field-adornment-icon is-blocked" aria-hidden="true" /> : <ChevronDown className="field-adornment-icon is-editable" aria-hidden="true" />}
        </span>
      )}
      {open && (
        <OptionMenu ref={menu} className={`dd-menu${up ? ' is-up' : ''}`} onCancel={() => close()}>
          {items.map((item) => {
            const heading = item.group !== undefined && item.group !== group ? item.group : undefined
            group = item.group
            const chosen = item.value === value
            return [
              heading !== undefined && <div key={`group:${heading}`} className="dd-group">{heading}</div>,
              <MenuOption key={item.value} className="dd-option" selected={chosen} disabled={item.disabled} title={item.title} onChoose={() => { onChange(item.value); close() }}>
                <span className="dd-check">{chosen && <Check aria-hidden="true" />}</span>
                <span className="dd-label">{item.label}</span>
                {item.hint && <span className="dd-hint">{item.hint}</span>}
                {item.badge && <Badge tone="warning">{item.badge}</Badge>}
              </MenuOption>,
            ]
          })}
        </OptionMenu>
      )}
    </div>
  )
}
