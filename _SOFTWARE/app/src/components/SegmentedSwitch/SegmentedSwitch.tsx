import './SegmentedSwitch.css'

/** A row of options of which exactly one is on (the block editor's switch): palettes, modes, a two-way setting. */
export function SegmentedSwitch<T extends string>({ value, options, onChange, label, className = '' }: {
  value: T
  options: readonly (readonly [T, string])[]
  onChange: (value: T) => void
  label: string
  className?: string
}) {
  return (
    <div className={`segmented-switch ${className}`.trim()} role="group" aria-label={label}>
      {options.map(([key, text]) => (
        <button key={key} type="button" aria-pressed={value === key} onClick={() => onChange(key)}>{text}</button>
      ))}
    </div>
  )
}
