import { decodePlacement, descriptorOf } from './blockModel'
import './Blocks.css'

const range = (min: number, max: number): string => (min === max ? String(min) : `${min} – ${max}`)

/** What a block is: its name, what it does and how many pins it has. */
export function BlockInfo({ placement }: { placement: string | undefined }) {
  const picked = placement === undefined ? undefined : decodePlacement(placement)
  const block = picked && descriptorOf(picked.typeKey)
  if (!block) return <div className="block-info"><p>Pick a block to read about it. Drag it onto the canvas to use it.</p></div>
  const op = block.ops?.find((entry) => entry.value === Object.values(picked.settings ?? {})[0])
  const { shape } = block
  return (
    <div className="block-info">
      <h1 className={`block-info-title cat-${block.category}`}><span className="block-palette-swatch block-info-swatch" aria-hidden="true" />{block.alias}</h1>
      <p className="block-info-kicker">{block.category}</p>
      <p>{block.description}</p>
      {op && <p><strong>{op.alias}</strong>: {op.description}</p>}
      <dl>
        <dt>Inputs</dt><dd>{range(shape.min_in, shape.max_in)}</dd>
        <dt>Outputs</dt><dd>{range(shape.min_q, shape.max_q)}</dd>
        <dt>Enables</dt><dd>{shape.min_en}</dd>
      </dl>
    </div>
  )
}
