export function BlockSwitch({ label, value, options, onChange, inline = false }: {
  label: string
  value: string
  options: readonly [readonly [string, string], readonly [string, string]]
  onChange: (value: string) => void
  /** Just the switch, without its label row: for the end of a heading. */
  inline?: boolean
}) {
  const group = <div className={`block-switch${inline ? ' is-inline' : ''}`} role="group" aria-label={label}>{options.map(([key, text]) => <button key={key} type="button" aria-pressed={value === key} onClick={() => onChange(key)}>{text}</button>)}</div>
  return inline ? group : <div className="block-field"><span>{label}</span>{group}</div>
}
