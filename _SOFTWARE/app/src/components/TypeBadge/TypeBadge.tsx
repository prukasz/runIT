import './TypeBadge.css'
import { Link2, ToggleLeft, Type } from 'lucide-react'
import { Badge } from '../Badge'

/** The same compact kind badge used for object values and block pins. */
export function ValueKindBadge({ type, title }: { type: string; title?: string }) {
  const category = /^(b|bool|boolean|gate)$/i.test(type) ? 'bool' : /^(str|string)$/i.test(type) ? 'text' : /^(object|ptr-cell)$/i.test(type) ? 'reference' : 'number'
  const label = category === 'bool' ? 'Boolean (T/F)' : category === 'text' ? 'String text' : category === 'reference' ? 'Object' : `${type} number`
  return <span className={`object-type-icon ${category}`} title={title ?? label} aria-hidden="true">
    {category === 'bool' ? <ToggleLeft /> : category === 'text' ? <Type /> : category === 'reference' ? <Link2 /> : <span className="object-number-glyph">123</span>}
  </span>
}

/** Compact, shared label for a value or pin type. Arrays can show their length beside it. */
export function TypeBadge({ type, count, title }: { type: string; count?: number; title?: string }) {
  const tone = /^(b|bool|boolean|gate)$/i.test(type) ? 'boolean' : /^(str|string)$/i.test(type) ? 'text' : /^(object|ptr-cell|raw)$/i.test(type) ? 'other' : 'number'
  return <span className="type-badge-group" title={title ?? type}>
    {count !== undefined && <Badge tone="count">{count}</Badge>}
    <Badge tone={tone}>{type}</Badge>
  </span>
}
