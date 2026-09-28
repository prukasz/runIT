import { useId, useState } from 'react'
import type { ReactNode } from 'react'
import { Info } from 'lucide-react'
import './FieldNote.css'

/**
 * A field's label and, when the field has a note (the header's `@note`), an (i)
 * button next to it: a click opens the note under the label, another closes it.
 * With `htmlFor` the text is a real <label> for that control.
 */
export function FieldLabel({ htmlFor, note, children, className = '' }: { htmlFor?: string; note?: string; children: ReactNode; className?: string }) {
  const [open, setOpen] = useState(false)
  const noteId = useId()
  return (
    <>
      <span className={`field-label ${className}`}>
        {htmlFor ? <label htmlFor={htmlFor}>{children}</label> : <span>{children}</span>}
        {note && (
          <button type="button" className={`field-note-toggle ${open ? 'is-open' : ''}`} aria-label={open ? 'Hide note' : 'Show note'} aria-expanded={open} aria-controls={noteId} title={open ? 'Hide note' : 'Show note'} onClick={() => setOpen((value) => !value)}>
            <Info aria-hidden="true" />
          </button>
        )}
      </span>
      {note && <span id={noteId} className="field-note" role="note" hidden={!open}>{note}</span>}
    </>
  )
}
