// Input field: type a number and send it (a setpoint, a position). The built-in "input" widget as an importable file.
// Its kind is "input-js" because built-in kinds are reserved.

export default {
  kind: 'input-js',
  title: 'Input field (file)',
  description: 'Type a number and send it: a setpoint, a position.',
  size: { w: 4, h: 2, minW: 3, minH: 2 },
  advanced: ['step', 'send'],
  slots: [{ key: 'value', label: 'Value', writes: true }],
  fields: [
    { key: 'min', label: 'Lowest value', type: 'number', default: -1000000 },
    { key: 'max', label: 'Highest value', type: 'number', default: 1000000 },
    { key: 'step', label: 'Step of the arrows', type: 'number', default: 1, min: 0 },
    { key: 'unit', label: 'Unit', type: 'text', default: '' },
    { key: 'send', label: 'Send', type: 'choice', default: 'enter', choices: [['enter', 'On Enter / Send'], ['change', 'On every change']] },
  ],

  mount(element, api) {
    element.style.cssText = 'display:flex;align-items:center;gap:6px;padding:0 10px;height:100%;box-sizing:border-box;'
    const form = document.createElement('form')
    form.style.cssText = 'display:flex;flex:1;align-items:center;gap:6px;min-width:0;'
    const input = document.createElement('input')
    input.type = 'number'
    input.placeholder = '–'
    input.style.cssText = 'flex:1;min-width:0;height:30px;padding:0 8px;border:1px solid var(--divider);border-radius:5px;background:var(--background);color:var(--active);font-size:14px;font-variant-numeric:tabular-nums;'
    const unit = document.createElement('span')
    unit.style.cssText = 'color:var(--muted);font-size:12px;'
    const submit = document.createElement('button')
    submit.type = 'submit'
    submit.textContent = 'Send'
    submit.style.cssText = 'width:auto;height:30px;padding:0 10px;border:1px solid var(--divider);border-radius:5px;background:var(--accent);color:var(--on-accent);font-size:12px;cursor:pointer;'
    form.append(input, unit, submit)
    element.append(form)

    let options = { min: -1000000, max: 1000000, step: 1, unit: '', send: 'enter' }
    let draft // what is typed and not sent yet
    let shown // what the board reports
    const parsed = () => (draft === undefined || draft.trim() === '' ? undefined : Number(draft))
    const valid = () => {
      const value = parsed()
      return value !== undefined && Number.isFinite(value) && value >= Math.min(options.min, options.max) && value <= Math.max(options.min, options.max)
    }
    const paint = () => {
      if (draft !== undefined) input.value = draft
      else input.value = shown === undefined ? '' : String(shown)
      input.style.borderColor = draft !== undefined && !valid() ? 'var(--color-red)' : 'var(--divider)'
      submit.disabled = !valid() || input.disabled
      submit.style.background = submit.disabled ? 'transparent' : 'var(--accent)'
      submit.style.color = submit.disabled ? 'var(--muted)' : 'var(--on-accent)'
    }
    input.addEventListener('input', () => {
      draft = input.value
      if (options.send === 'change' && valid()) api.write('value', parsed())
      paint()
    })
    input.addEventListener('blur', () => { if (options.send === 'change') { draft = undefined; paint() } })
    form.addEventListener('submit', (event) => {
      event.preventDefault()
      if (valid()) { api.write('value', parsed()); draft = undefined; paint() }
    })

    return {
      update(state) {
        options = state.options
        input.setAttribute('aria-label', state.name || 'Value to send')
        shown = state.values.value
        input.disabled = state.preview
        input.min = String(Math.min(options.min, options.max))
        input.max = String(Math.max(options.min, options.max))
        input.step = options.step > 0 ? String(options.step) : 'any'
        unit.textContent = options.unit
        unit.style.display = options.unit ? '' : 'none'
        submit.style.display = options.send === 'change' ? 'none' : ''
        paint()
      },
    }
  },
}
