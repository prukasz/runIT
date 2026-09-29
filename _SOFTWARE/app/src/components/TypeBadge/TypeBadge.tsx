import './TypeBadge.css'
import { Link2 } from 'lucide-react'

/** The same compact kind badge used for object values and block pins. */
export function ValueKindBadge({ type, title }: { type: string; title?: string }) {
  const category = /^(b|bool|boolean|gate)$/i.test(type) ? 'bool' : /^(str|string)$/i.test(type) ? 'text' : /^(object|ptr-cell)$/i.test(type) ? 'reference' : 'number'
  const label = category === 'bool' ? 'Boolean (T/F)' : category === 'text' ? 'String text' : category === 'reference' ? 'Object' : `${type} number`
  return <span className={`object-type-icon ${category}`} title={title ?? label} aria-hidden="true">
    {category === 'bool' ? <span className="object-bool-glyph">T/F</span> : category === 'text' ? <span className="object-text-glyph">T</span> : category === 'reference' ? <Link2 /> : <span className="object-number-glyph">1.2.3</span>}
  </span>
}

/** Compact, shared label for a value or pin type. Arrays can show their length beside it. */
export function typeKindLabel(type: string): string {
  if (/^(b|bool|boolean|gate)$/i.test(type)) return 'Bool'
  if (/^(str|string)$/i.test(type)) return 'String'
  if (/^(object|ptr-cell)$/i.test(type)) return 'Object'
  if (/^raw$/i.test(type)) return 'Data'
  return 'Number'
}

export function TypeBadge({ type, count, title, label }: { type: string; count?: number; title?: string; label?: string }) {
  const tone = /^(b|bool|gate)$/i.test(type) ? 'bool' : /^(str|string)$/i.test(type) ? 'text' : /^(object|ptr-cell|raw)$/i.test(type) ? 'other' : 'number'
  return <span className="type-badge-group" title={title ?? type}>
    {count !== undefined && <span className="type-badge-count">{count}</span>}
    <span className={`type-badge is-${tone}`}>{label ?? type}</span>
  </span>
}
