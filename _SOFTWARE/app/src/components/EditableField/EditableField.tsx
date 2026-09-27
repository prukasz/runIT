import React from 'react'
import { Pencil, PencilOff } from 'lucide-react'
import './EditableField.css'

export interface EditableFieldProps extends Omit<React.InputHTMLAttributes<HTMLInputElement>, 'onChange'> {
  value: string
  onChange: (value: string, e: React.ChangeEvent<HTMLInputElement>) => void
  disabled?: boolean
  selected?: boolean
  iconTitle?: string
  showIcon?: boolean
  className?: string
  inputClassName?: string
}

export const EditableField: React.FC<EditableFieldProps> = ({
  value,
  onChange,
  disabled = false,
  selected = false,
  iconTitle,
  showIcon = true,
  className = '',
  inputClassName = '',
  title,
  placeholder,
  maxLength,
  onBlur,
  onClick,
  ...rest
}) => {
  const isBlocked = disabled
  const statusClass = isBlocked ? 'is-blocked' : selected ? 'is-selected' : 'is-editable'
  const computedIconTitle = iconTitle || (isBlocked ? 'Locked (Read-only)' : 'Editable field')

  return (
    <div className={`editable-field-wrap ${statusClass} ${className}`.trim()}>
      <input
        type="text"
        className={`editable-field-input ${inputClassName}`.trim()}
        value={value}
        disabled={disabled}
        title={title}
        placeholder={placeholder}
        maxLength={maxLength}
        onClick={onClick}
        onBlur={onBlur}
        onChange={(e) => onChange(e.target.value, e)}
        {...rest}
      />
      {showIcon && (
        <span className="editable-field-icon-slot" title={computedIconTitle}>
          {isBlocked ? (
            <PencilOff className="editable-field-icon is-blocked" aria-hidden="true" />
          ) : (
            <Pencil className="editable-field-icon is-editable" aria-hidden="true" />
          )}
        </span>
      )}
    </div>
  )
}
