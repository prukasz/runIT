import { useRef } from 'react'
import { Search, X } from 'lucide-react'
import { TextField } from '../FormField'
import './PaletteSearch.css'

export function PaletteSearch({ value, onChange, placeholder, label }: {
  value: string
  onChange: (value: string) => void
  placeholder: string
  label: string
}) {
  const input = useRef<HTMLInputElement>(null)
  const clear = () => {
    onChange('')
    input.current?.focus()
  }

  return <div className="palette-search">
    <Search className="palette-search-icon" aria-hidden="true" />
    <TextField ref={input} type="text" placeholder={placeholder} aria-label={label} value={value}
      onChange={(event) => onChange(event.target.value)}
      onKeyDown={(event) => { if (event.key === 'Escape' && value) { event.preventDefault(); clear() } }} />
    {value && <button type="button" className="palette-search-clear" title="Clear search" aria-label="Clear search" onClick={clear}><X aria-hidden="true" /></button>}
  </div>
}
