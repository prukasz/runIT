# Custom controller widgets

A widget for the Remote view is **one `.js` file**. Import it in the Widgets tab (**Import widget**); it then sits in the palette next to the built-in ones, with the same right panel (variables, settings, position), linking by drag and drop, undo, and saving: the file's text is stored in the project.

The file is the user's own code and runs inside the app, with everything the page can do (including the board connection). Import only files you wrote or trust.

[example.widget.js](controller/example.widget.js) is a complete, working widget; **Get template** in the palette downloads it.

## The file

An ES module whose default export describes the widget:

```js
export default {
  kind: 'led-meter',            // required: 2-32 chars, a lower-case letter, then letters, digits, "-"
  title: 'LED meter',           // required: the name in the palette
  description: '…',             // shown in the palette and the panel
  size: { w: 5, h: 2, minW: 3, minH: 1 },   // grid cells
  keyboard: false,              // true: needs a physical keyboard (the palette marks it "laptop")
  advanced: ['max'],            // keys of `slots` and `fields` the panel keeps behind "Advanced" (the rest are shown open)
  slots:  [{ key: 'level', label: 'Level', writes: true, hint: '…' }],
  fields: [{ key: 'max', label: 'Highest value', type: 'number', default: 100 }],
  mount(element, api) { /* draw */ return { update(state) {}, destroy() {} } },
}
```

| Part | Meaning |
|---|---|
| `kind` | Names the widget in the project. Two files cannot provide the same kind, and a built-in kind is refused. Changing a file's `kind` orphans the widgets already placed. |
| `slots` | The variables the widget uses. `writes: true`: the widget sets it, otherwise it only reads. The user links each by dropping a variable on its row in the panel (only single values). `key`: letters, digits, `_`. |
| `fields` | The settings. `type`: `number` (`default`, optional `min` / `max`), `bool`, `text`, `key` (a keyboard key, pressed to set), `choice` (`choices`: `[value, label]` pairs). Every field has a `key`, a `label`, a `default`, optionally a `hint`. |

## `mount(element, api)`

Called once when the widget is shown. Draw into `element` (a div the size of the widget; use plain DOM, or any library the file brings itself). `api` has one thing:

- `api.write(slot, number)`: set the variable linked to a slot that `writes`. The app throttles it (about 20 per second per variable) and ignores it for an unlinked slot or while the interface is not enabled.

Return an object, both parts optional:

- `update(state)`: called after `mount` and whenever something changed.
  - `state.values`: the live value of each slot's variable (`number`, or `undefined` while not enabled or not linked).
  - `state.options`: the settings, defaults filled in.
  - `state.name`: the name the user gave the widget (empty when none), for `aria-label`s.
  - `state.linked`: for each slot, whether a variable is linked (`values` is `undefined` both for an unlinked slot and while the interface is off; this tells which).
  - `state.live`: the interface is enabled (the widget works).
  - `state.preview`: the layout is being edited; the widget should only show itself.
  - `state.width`, `state.height`: its area in pixels.
- `destroy()`: called when the widget is removed or its file replaced; remove timers and listeners you added on `window`.

If the file throws (in `mount` or `update`), the message is shown in the widget; the rest of the app is not affected.

## Built-in widgets as files

[widgets/](controller/widgets/) has every built-in widget (joystick, slider, switch, button, input, keyboard, value, chart, lamp, bar, label) as a working file of this format: the same slots, settings, size and behaviour, to read, copy and change. Their kinds end in `-js` (`slider-js` ...), because a built-in kind cannot be taken. `tests/controller/exportedWidgets.test.ts` keeps them in step with the built-in ones.

## Limits

- The file must be at most 200 KB and a single module with no `import`s of other local files (a URL import works if the browser allows it).
- Values are numbers. Text variables cannot be linked.
- The app calls `update` only when something in the state changes. A widget that needs a clock (the chart's sampling) keeps its own timer and clears it in `destroy`.
- The widget's look should use the page's colours: the CSS variables of the shell (`var(--panel)`, `var(--active)`, `var(--accent)`, …) work inside `element`.
