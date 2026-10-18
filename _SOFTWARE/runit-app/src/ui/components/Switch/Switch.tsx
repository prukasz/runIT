import './Switch.css'

/** The one on/off control: the thumb sits left when off (no fill), right when on (green). Use it for every setting that is on or off. */
export function Switch({ checked, onChange, label, disabled, tone = 'on' }: {
  checked: boolean
  onChange: (checked: boolean) => void
  label: string
  disabled?: boolean
  /** `on`: empty when off, green when on. `truefalse`: red when off (false), green when on (true). */
  tone?: 'on' | 'truefalse'
}) {
  return (
    <label className={`switch ${tone === 'truefalse' ? 'is-truefalse' : ''} ${disabled ? 'is-disabled' : ''}`}>
      <input type="checkbox" checked={checked} disabled={disabled} onChange={(event) => onChange(event.target.checked)} />
      <span className="switch-track" aria-hidden="true" />
      <span className="switch-label">{label}</span>
    </label>
  )
}
