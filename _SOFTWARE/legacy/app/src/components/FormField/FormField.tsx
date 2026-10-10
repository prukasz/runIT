import { forwardRef } from 'react'
import type { InputHTMLAttributes, SelectHTMLAttributes } from 'react'
import './FormField.css'

/** Shared native controls. Keep native events, keyboard behavior, and form semantics. */
export const TextField = forwardRef<HTMLInputElement, InputHTMLAttributes<HTMLInputElement>>(function TextField({ className = '', type, ...props }, ref) {
  const nativeControl = type === 'checkbox' || type === 'radio' || type === 'file' || type === 'hidden' || type === 'range' || type === 'color'
  return <input ref={ref} type={type} className={nativeControl ? className || undefined : `form-field-control ${className}`.trim()} {...props} />
})

export const SelectField = forwardRef<HTMLSelectElement, SelectHTMLAttributes<HTMLSelectElement>>(function SelectField({ className = '', ...props }, ref) {
  return <select ref={ref} className={`form-field-control form-select-control ${className}`.trim()} {...props} />
})
