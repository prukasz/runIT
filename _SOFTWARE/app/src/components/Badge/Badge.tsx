import type { ReactNode } from 'react'
import './Badge.css'

export type BadgeTone = 'neutral' | 'system' | 'count' | 'number' | 'boolean' | 'text' | 'other' | 'success' | 'warning' | 'info' | 'accent'
export type BadgeSize = 'compact' | 'default' | 'detail'

export function Badge({ children, tone = 'neutral', size = 'default', title, className, push = false, caps = false, filled = false }: {
  children: ReactNode
  tone?: BadgeTone
  size?: BadgeSize
  title?: string
  className?: string
  push?: boolean
  caps?: boolean
  filled?: boolean
}) {
  return <span className={`badge is-${tone} is-${size}${push ? ' is-pushed' : ''}${caps ? ' is-caps' : ''}${filled ? ' is-filled' : ''}${className ? ` ${className}` : ''}`} title={title}>
    {children}
  </span>
}
