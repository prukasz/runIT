import React from 'react'
import { ChevronDown, Lock } from 'lucide-react'
import { SelectField } from '../FormField'
import './ListableField.css'

export interface ListableOptionObject {
  value: string
  label: string
  disabled?: boolean
  /** Hover text: what the option means. */
  title?: string
  /** Options sharing a group are listed under its heading (the first option of a group places it). */
  group?: string
}

export type ListableOption = string | ListableOptionObject

export interface ListableFieldProps extends Omit<React.SelectHTMLAttributes<HTMLSelectElement>, 'onChange'> {
  value: string
  onChange: (value: string, e: React.ChangeEvent<HTMLSelectElement>) => void
  options: ListableOption[]
  disabled?: boolean
  selected?: boolean
  iconTitle?: string
  showIcon?: boolean
  className?: string
  selectClassName?: string
  /** Size overrides (a number is px, a string any CSS length). Unset ones follow the surrounding `--field-*` variables. */
  width?: number | string
  minWidth?: number | string
  height?: number | string
  fontSize?: number | string
  fontWeight?: number | string
  padding?: number | string
  radius?: number | string
}

const renderOption = (opt: ListableOption) => {
  const object = typeof opt === 'string' ? { value: opt, label: opt } : opt
  return <option key={object.value} value={object.value} disabled={Boolean(object.disabled)} title={object.title}>{object.label}</option>
}

/** Options in order; the ones of a group sit together under an `optgroup` where its first option was. */
const renderOptions = (options: ListableOption[]) => {
  const groups = [...new Set(options.map((opt) => (typeof opt === 'string' ? undefined : opt.group)))]
  return groups.flatMap((group) => {
    const members = options.filter((opt) => (typeof opt === 'string' ? undefined : opt.group) === group)
    return group === undefined ? members.map(renderOption) : [<optgroup key={`group:${group}`} label={group}>{members.map(renderOption)}</optgroup>]
  })
}

export const ListableField: React.FC<ListableFieldProps> = ({
  value,
  onChange,
  options,
  disabled = false,
  selected = false,
  iconTitle,
  showIcon = true,
  className = '',
  selectClassName = '',
  width,
  minWidth,
  height,
  fontSize,
  fontWeight,
  padding,
  radius,
  title,
  onClick,
  ...rest
}) => {
  const isBlocked = disabled
  const statusClass = isBlocked ? 'is-blocked' : selected ? 'is-selected' : 'is-editable'
  const computedIconTitle = iconTitle || (isBlocked ? 'Locked dropdown' : 'Select option')

  const length = (value: number | string | undefined) => (typeof value === 'number' ? `${value}px` : value)
  const style = {
    ...(width !== undefined ? { width: length(width), flex: 'none' } : {}),
    ...(minWidth !== undefined ? { '--listable-min-width': length(minWidth) } : {}),
    ...(height !== undefined ? { '--field-height': length(height) } : {}),
    ...(fontSize !== undefined ? { '--field-font-size': length(fontSize) } : {}),
    ...(fontWeight !== undefined ? { '--listable-font-weight': String(fontWeight) } : {}),
    ...(padding !== undefined ? { '--field-padding': length(padding) } : {}),
    ...(radius !== undefined ? { '--field-radius': length(radius) } : {}),
  } as React.CSSProperties

  return (
    <div className={`adorned-field-wrap listable-field-wrap ${statusClass} ${className}`.trim()} style={style}>
      <SelectField
        className={`listable-field-select ${selectClassName}`.trim()}
        value={value}
        disabled={disabled}
        title={title}
        onClick={onClick}
        onChange={(e) => onChange(e.target.value, e)}
        {...rest}
      >
        {renderOptions(options)}
      </SelectField>
      {showIcon && (
        <span className="field-adornment-slot" title={computedIconTitle}>
          {isBlocked ? (
            <Lock className="field-adornment-icon is-blocked" aria-hidden="true" />
          ) : (
            <ChevronDown className="field-adornment-icon is-editable" aria-hidden="true" />
          )}
        </span>
      )}
    </div>
  )
}
