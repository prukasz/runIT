import { Badge } from '../../components/Badge'
import { PanelHeader } from '../../components/PanelHeader/PanelHeader'
import { TypeBadge } from '../../components/TypeBadge/TypeBadge'
import { runitVmCatalog } from '../../domain/descriptors'
import { enumAlias, enumMemberLabels } from '../../domain/descriptors/vmBlocks'
import type { VmBlockPin, VmBlockType } from '../../domain/descriptors'
import type { ProjectCanvas } from '../../domain/project'
import { Markdown } from '../../devices/Markdown'
import '../Canvas.css'

/*
 * The Code view's block manager (main screen of View / Manage with the Blocks palette): what the block type picked in the tile
 * palette is, from its descriptor (display.json + content.json) and its user guide (app/docs/blocks/<key>.md), and where the
 * program uses it. Nothing here names a block.
 */

/** Block guides: app/docs/blocks/<key lowercase>.md, copied from each block's folder by generate-vm-blocks.py. */
const GUIDES: Readonly<Record<string, string>> = Object.fromEntries(
  Object.entries(import.meta.glob<string>('../../../docs/blocks/*.md', { query: '?raw', import: 'default', eager: true }))
    .map(([path, text]) => [path.replace(/^.*\/(.*)\.md$/, '$1').toUpperCase(), text.replace(/^<!--.*?-->\s*/s, '')]),
)

const CATEGORY_TITLES: Readonly<Record<string, string>> = { flow: 'Flow', data: 'Data', logic: 'Logic', time: 'Time', io: 'Pins', system: 'System' }

const pinRole = (pin: VmBlockPin, constantField: string | undefined): string => {
  if (pin.required) return 'required'
  if (constantField) return `optional: while unwired uses "${constantField}"`
  return 'optional'
}

function PinTable({ title, type, pins }: { title: string; type: VmBlockType; pins: readonly VmBlockPin[] }) {
  if (!pins.length) return null
  return (
    <section className="block-manager-section">
      <h2>{title}</h2>
      <dl className="block-manager-pins">
        {pins.map((pin) => {
          const field = pin.overrides ? type.fields.find((entry) => entry.name === pin.overrides) : undefined
          return (
            <div key={`${pin.index}-${pin.name}`}>
              <dt><span className="block-pin-title">{pin.title}{String(pin.index) === '*' && <small>any number</small>}</span><TypeBadge type={pin.value} /></dt>
              <dd>{pin.description ?? ''}<small>{pinRole(pin, field?.name)}</small></dd>
            </div>
          )
        })}
      </dl>
    </section>
  )
}

export function BlockManager({ typeKey }: { typeKey: string | undefined }) {
  const catalog = runitVmCatalog()
  const type = typeKey ? catalog.block(typeKey) : undefined
  if (!type) {
    return (
      <div className="object-editor block-manager">
        <PanelHeader title="Blocks" />
        <p className="block-manager-empty">Pick a block in the palette to read what it does, which pins and settings it has, and where the program uses it.</p>
      </div>
    )
  }
  const guide = GUIDES[type.key]
  const settings = type.fields.filter((field) => field.source === 'user' && !field.flexible)
  return (
    <div className={`object-editor block-manager cat-${type.category}`}>
      <PanelHeader icon={<span className="block-details-swatch" aria-hidden="true" />} title={type.title}>
        <Badge tone="info">{CATEGORY_TITLES[type.category] ?? type.category}</Badge>
        <Badge tone="number" title="Block type id on the wire">{type.id}</Badge>
        <Badge>{type.key}</Badge>
      </PanelHeader>

      <p className="block-manager-lead">{type.description}</p>
      <p className="block-manager-activation">{type.activationText ?? type.activation}</p>

      <PinTable title="Inputs" type={type} pins={type.inputs.pins} />
      <PinTable title="Outputs" type={type} pins={type.outputs.pins} />

      <section className="block-manager-section">
        <h2>Run when and ENO</h2>
        <dl className="block-manager-pins">
          <div><dt><span className="block-pin-title">Run when</span></dt><dd>Gates the block: with nothing connected it always runs.</dd></div>
          <div><dt><span className="block-pin-title">{type.eno.title}</span></dt><dd>{type.eno.description}</dd></div>
        </dl>
      </section>

      {settings.length > 0 && (
        <section className="block-manager-section">
          <h2>Settings</h2>
          <dl className="block-manager-pins">
            {settings.map((field) => {
              const options = field.enumRef ? type.enums.get(field.enumRef) : undefined
              return (
                <div key={field.name}>
                  <dt><span className="block-pin-title">{field.name}</span><TypeBadge type={field.cType} /></dt>
                  <dd>
                    {field.description ?? ''}
                    {options && <small>{enumMemberLabels(options).map((option, index) => options[index].alias ?? enumAlias(option.label)).join(' · ')}</small>}
                  </dd>
                </div>
              )
            })}
          </dl>
        </section>
      )}

      {type.rules.length > 0 && (
        <section className="block-manager-section">
          <h2>The board refuses the program when</h2>
          <ul className="block-manager-rules">
            {type.rules.map((rule) => <li key={rule.rule}>{rule.rule} <Badge size="compact" tone="warning">{rule.error}</Badge></li>)}
          </ul>
        </section>
      )}

      <section className="block-manager-section block-manager-guide">
        <h2>Guide</h2>
        {guide ? <Markdown source={guide} /> : <p className="block-manager-empty">No user guide for this block yet (app/docs/blocks/{type.key.toLowerCase()}.md).</p>}
      </section>
    </div>
  )
}

/** Right panel of View / Manage with the Blocks palette: the program's blocks of the picked type, each opening on its canvas. */
export function BlockUses({ typeKey, canvases, onOpenBlock }: {
  typeKey: string | undefined
  canvases: readonly ProjectCanvas[]
  onOpenBlock: (canvasId: string, blockId: string) => void
}) {
  const type = typeKey ? runitVmCatalog().block(typeKey) : undefined
  if (!type) return <p className="block-manager-empty">Pick a block in the palette to see where the program uses it.</p>
  const used = canvases.map((canvas) => ({ canvas, blocks: canvas.blocks.filter((block) => block.type === type.key) })).filter((entry) => entry.blocks.length)
  const count = used.reduce((sum, entry) => sum + entry.blocks.length, 0)
  return (
    <div className={`block-uses cat-${type.category}`}>
      <PanelHeader icon={<span className="block-details-swatch" aria-hidden="true" />} title={`${type.title} in the program`}>
        <Badge size="compact" tone="count">{count}</Badge>
      </PanelHeader>
      {count === 0
        ? <p className="block-manager-empty">No {type.title} block in the program yet.</p>
        : used.map(({ canvas, blocks }) => (
          <section key={canvas.id} className="block-uses-canvas">
            <h2>{canvas.name}</h2>
            <ul className="block-manager-uses">
              {blocks.map((block) => (
                <li key={block.id}>
                  <button type="button" onClick={() => onOpenBlock(canvas.id, block.id)} title="Open on the canvas"><strong>{block.id}</strong></button>
                </li>
              ))}
            </ul>
          </section>
        ))}
    </div>
  )
}
