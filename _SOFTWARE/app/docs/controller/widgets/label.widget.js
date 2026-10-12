// Label: text to name a group of widgets. The built-in "label" widget as an importable file.
// Its kind is "label-js" because built-in kinds are reserved.

const SIZES = { small: '12px', normal: '16px', large: '24px' }
const COLORS = { text: 'var(--active)', accent: 'var(--accent)', green: 'var(--color-green)', amber: 'var(--color-amber)', red: 'var(--color-red)', blue: 'var(--color-blue)', purple: 'var(--color-purple)', orange: 'var(--color-orange)' }
const TEXT_COLOR_CHOICES = [['text', 'Text'], ['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]

export default {
  kind: 'label-js',
  title: 'Label (file)',
  description: 'Text to name a group of widgets.',
  size: { w: 4, h: 1, minW: 1, minH: 1 },
  advanced: ['align'],
  slots: [],
  fields: [
    { key: 'text', label: 'Text', type: 'text', default: 'Label' },
    { key: 'size', label: 'Size', type: 'choice', default: 'normal', choices: [['small', 'Small'], ['normal', 'Normal'], ['large', 'Large']] },
    { key: 'color', label: 'Colour', type: 'choice', default: 'text', choices: TEXT_COLOR_CHOICES },
    { key: 'align', label: 'Alignment', type: 'choice', default: 'left', choices: [['left', 'Left'], ['center', 'Centre']] },
  ],

  mount(element) {
    element.style.cssText = 'display:flex;align-items:center;padding:0 10px;overflow:hidden;font-weight:600;height:100%;box-sizing:border-box;'
    return {
      update({ options }) {
        element.textContent = options.text
        element.style.fontSize = SIZES[options.size] ?? SIZES.normal
        element.style.color = COLORS[options.color] ?? COLORS.text
        element.style.justifyContent = options.align === 'center' ? 'center' : 'flex-start'
      },
    }
  },
}
