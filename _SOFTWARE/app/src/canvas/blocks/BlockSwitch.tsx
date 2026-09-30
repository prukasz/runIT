export function BlockSwitch({ label, value, options, onChange }: { label: string; value: string; options: readonly [readonly [string, string], readonly [string, string]]; onChange: (value: string) => void }) {
  return <div className="block-field"><span>{label}</span><div className="block-switch" role="group" aria-label={label}>{options.map(([key, text]) => <button key={key} type="button" aria-pressed={value === key} onClick={() => onChange(key)}>{text}</button>)}</div></div>
}
