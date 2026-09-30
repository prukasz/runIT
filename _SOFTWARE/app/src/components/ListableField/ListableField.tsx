import React from 'react'
import { ChevronDown, Lock } from 'lucide-react'
import { SelectField } from '../FormField'
import './ListableField.css'

export interface ListableOptionObject {
  value: string
  label: string
  disabled?: boolean
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
  title,
  onClick,
  ...rest
}) => {
  const isBlocked = disabled
  const statusClass = isBlocked ? 'is-blocked' : selected ? 'is-selected' : 'is-editable'
  const computedIconTitle = iconTitle || (isBlocked ? 'Locked dropdown' : 'Select option')

  return (
    <div className={`adorned-field-wrap listable-field-wrap ${statusClass} ${className}`.trim()}>
      <SelectField
        className={`listable-field-select ${selectClassName}`.trim()}
        value={value}
        disabled={disabled}
        title={title}
        onClick={onClick}
        onChange={(e) => onChange(e.target.value, e)}
        {...rest}
      >
        {options.map((opt) => {
          const optValue = typeof opt === 'string' ? opt : opt.value
          const optLabel = typeof opt === 'string' ? opt : opt.label
          const optDisabled = typeof opt === 'string' ? false : Boolean(opt.disabled)

          return (
            <option key={optValue} value={optValue} disabled={optDisabled}>
              {optLabel}
            </option>
          )
        })}
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
