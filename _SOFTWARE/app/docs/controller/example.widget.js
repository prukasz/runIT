// A runIT controller widget: one .js file, imported from the Remote view (Widgets tab > Import).
// This one is a row of LEDs that fills up with a variable, and a button that sets the variable to its top.
// The file is the user's own code and runs inside the app, so import only files you trust.

export default {
  // A short unique name: a lower-case letter, then letters, digits or "-". Not one of the built-in kinds.
  kind: 'led-meter',
  title: 'LED meter',
  description: 'A row of LEDs that follows a value; the button jumps to the top.',

  // Size on the grid, in cells (48 px each). `w` / `h` is the size it is placed with.
  size: { w: 5, h: 2, minW: 3, minH: 1 },

  // The variables the widget uses. `writes: true` = the widget sets it, otherwise it only reads it.
  // The user links each slot by dropping a variable on it in the right panel.
  slots: [
    { key: 'level', label: 'Level', writes: true },
  ],

  // The settings of the right panel. Types: number, bool, text, key (a keyboard key), choice ([value, label] pairs).
  fields: [
    { key: 'min', label: 'Lowest value', type: 'number', default: 0 },
    { key: 'max', label: 'Highest value', type: 'number', default: 100 },
    { key: 'leds', label: 'LEDs', type: 'number', default: 10, min: 2, max: 40 },
    { key: 'color', label: 'Colour', type: 'choice', default: 'green', choices: [['green', 'Green'], ['amber', 'Amber'], ['red', 'Red']] },
  ],

  // Draws the widget once into `element` (a div the size of the widget). `api.write(slot, number)` sets a variable.
  // Return { update, destroy }: `update(state)` is called when anything changed, `destroy()` when the widget goes away.
  mount(element, api) {
    element.style.cssText = 'display:flex;align-items:center;gap:8px;padding:0 10px;height:100%;box-sizing:border-box;'
    const row = document.createElement('div')
    row.style.cssText = 'display:flex;flex:1;gap:3px;height:60%;'
    const top = document.createElement('button')
    top.textContent = 'Max'
    top.style.cssText = 'height:70%;padding:0 8px;border:1px solid #666;border-radius:4px;background:transparent;color:inherit;cursor:pointer;'
    element.append(row, top)

    const colors = { green: '#50d782', amber: '#e9ad50', red: '#e5484d' }
    let max = 100

    top.addEventListener('click', () => api.write('level', max))

    return {
      // state = { values: { slot: number | undefined }, options: { key: value }, live, preview, width, height }
      update({ values, options, live, preview }) {
        const count = Math.max(2, Math.round(options.leds))
        max = options.max
        const span = options.max - options.min || 1
        const fraction = values.level === undefined ? 0 : Math.min(1, Math.max(0, (values.level - options.min) / span))
        const lit = Math.round(fraction * count)
        row.replaceChildren(...Array.from({ length: count }, (_, index) => {
          const led = document.createElement('span')
          led.style.cssText = `flex:1;border-radius:3px;background:${index < lit ? colors[options.color] : 'rgba(128,128,128,.25)'};`
          return led
        }))
        // While the layout is being edited (not live) nothing may be sent.
        top.disabled = preview || !live
      },
      destroy() {
        // Nothing to clean up here; remove timers or listeners on `window` in this function.
      },
    }
  },
}
