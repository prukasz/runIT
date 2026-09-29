import React from 'react'
import { Pencil, PencilOff } from 'lucide-react'
import { TextField } from '../FormField'
import './EditableField.css'

export interface EditableFieldProps extends Omit<React.InputHTMLAttributes<HTMLInputElement>, 'onChange'> {
  value: string
  onChange?: (value: string, e: React.ChangeEvent<HTMLInputElement>) => void
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
  type = 'text',
  maxLength,
  onBlur,
  onClick,
  ...rest
}) => {
  const isBlocked = disabled || rest.readOnly
  const statusClass = isBlocked ? 'is-blocked' : selected ? 'is-selected' : 'is-editable'
  const computedIconTitle = iconTitle || (isBlocked ? 'Locked (Read-only)' : 'Editable field')

  return (
    <div className={`adorned-field-wrap editable-field-wrap ${statusClass} ${className}`.trim()}>
      <TextField
        type={type}
        className={`editable-field-input ${inputClassName}`.trim()}
        value={value}
        disabled={disabled}
        title={title}
        placeholder={placeholder}
        maxLength={maxLength}
        onClick={onClick}
        onBlur={onBlur}
        onChange={(e) => onChange?.(e.target.value, e)}
        {...rest}
      />
      {showIcon && (
        <span className="field-adornment-slot" title={computedIconTitle}>
          {isBlocked ? (
            <PencilOff className="field-adornment-icon is-blocked" aria-hidden="true" />
          ) : (
            <Pencil className="field-adornment-icon is-editable" aria-hidden="true" />
          )}
        </span>
      )}
    </div>
  )
}
