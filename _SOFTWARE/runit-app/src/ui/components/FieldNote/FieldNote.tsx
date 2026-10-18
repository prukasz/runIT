import type { ReactNode } from 'react'
import { TriangleAlert } from 'lucide-react'
import './FieldNote.css'

/**
 * A field's label. Its help text is the row's `data-help` (FormRow's `note`), shown by help mode (HelpOverlay), so the label has
 * no note button of its own. With `htmlFor` the text is a real <label> for that control.
 */
export function FieldLabel({ htmlFor, children, className = '' }: { htmlFor?: string; children: ReactNode; className?: string }) {
  return (
    <span className={`field-label ${className}`}>
      {htmlFor ? <label htmlFor={htmlFor}>{children}</label> : <span>{children}</span>}
    </span>
  )
}

/** What is wrong with a field's value, under the field (error.css). Nothing when `children` is empty. */
export function FieldError({ children }: { children?: ReactNode }) {
  if (!children) return null
  return <span className="field-error" role="alert"><TriangleAlert aria-hidden="true" />{children}</span>
}
