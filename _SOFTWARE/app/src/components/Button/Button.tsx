import { forwardRef } from 'react'
import type { ButtonHTMLAttributes } from 'react'
import './Button.css'

export type ButtonVariant = 'default' | 'primary' | 'danger' | 'dashed'
/** `md` 32px, `sm` 28px, `icon` / `icon-sm` are square and hold only an icon (give them an aria-label). */
export type ButtonSize = 'md' | 'sm' | 'icon' | 'icon-sm'

interface ButtonStyle {
  variant?: ButtonVariant
  size?: ButtonSize
  /** Fill the row. */
  block?: boolean
  className?: string
}

/** Class list of a button; use it directly for an `<a>` that looks like one. */
export const buttonClass = ({ variant = 'default', size = 'md', block, className }: ButtonStyle = {}): string =>
  ['ui-button', `is-${variant}`, `is-${size}`, block ? 'is-block' : '', className ?? ''].filter(Boolean).join(' ')

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement>, ButtonStyle {}

/** The text button of the app (icon + label). Icon-only toolbar buttons keep the shell's plain button style. */
export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button({ variant, size, block, className, type = 'button', ...props }, ref) {
  return <button ref={ref} type={type} className={buttonClass({ variant, size, block, className })} {...props} />
})

/** A pill that toggles on and off (tags, filters). `selected` follows the shell's selected state. */
export const ToggleChip = forwardRef<HTMLButtonElement, ButtonHTMLAttributes<HTMLButtonElement> & { selected?: boolean }>(function ToggleChip({ selected, className = '', type = 'button', ...props }, ref) {
  return <button ref={ref} type={type} aria-pressed={selected} className={`ui-chip ${selected ? 'selected' : ''} ${className}`.trim()} {...props} />
})
