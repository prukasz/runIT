import { Button, FieldError, TreeAction } from '@ui/components'
import { useErrors } from '../console/useConsole'
import { blockDiagnostics, headline, isBlockTarget, pinsOf, sourceLabel } from './blocks'
import type { BlocksState } from './useBlocks'
import './Blocks.css'

/**
 * The centre of the Code view (details) while the palette shows the blocks: every block of the project in the order they run
 * (the engine's, from the wiring; a block the compiler could not place goes last, unnumbered), with its header, what its pins read and write, what it runs on, and what the
 * compiler says of it. A click selects: the right panel edits it.
 */
export function BlocksList({ blocks }: { blocks: BlocksState }) {
  const errors = useErrors()
  const { ctx, program } = blocks
  // A block picked in the palette: here it joins the end of the list (on the canvas a click says where it goes).
  const armedType = blocks.armed && ctx.catalog.get(blocks.armed.type)
  const placing = armedType && (
    <div className="block-list-placing">
      <span>{armedType.variants.find((v) => v.id === blocks.armed?.variant)?.alias ?? armedType.alias}: place it on the canvas, or add it here.</span>
      <Button size="sm" variant="primary" onClick={() => { blocks.add(blocks.armed!); blocks.arm(undefined) }}>Add to the program</Button>
      <Button size="sm" onClick={() => blocks.arm(undefined)}>Put back</Button>
    </div>
  )
  // The order of a pass as the engine arranges it (`CompileResult.order`).
  const runs = new Map(blocks.compiled.order.map((id, i) => [id, i]))
  const rank = (id: string) => runs.get(id) ?? Number.MAX_SAFE_INTEGER
  if (program.blocks.length === 0) return <div className="block-list">{placing}<div className="view-placeholder"><p>No blocks yet: pick one in the palette on the left.</p></div></div>
  return (
    <div className="block-list">
      {placing}
      {[...program.blocks].sort((a, b) => rank(a.id) - rank(b.id)).map((block) => {
        const type = ctx.catalog.get(block.type)
        const pins = pinsOf(ctx.catalog, block)
        const problems = blockDiagnostics(blocks.compiled.diagnostics, block.id).filter((d) => d.severity === 'error')
        const unread = errors?.console.aboutTarget((t) => isBlockTarget(t, block.id)).some((e) => !e.read) ?? false
        const classes = ['block-entry', `cat-${type?.category ?? 'unknown'}`, blocks.selectedIds.includes(block.id) && 'is-selected', (problems.length > 0 || unread) && 'is-error'].filter(Boolean).join(' ')
        const reads = (pins?.inputs ?? []).flatMap((p) => {
          const s = block.inputs[p.index]
          return s ? [{ key: `in${p.index}`, name: p.pin.alias, text: `← ${sourceLabel(ctx, s)}` }] : []
        })
        const writes = (pins?.outputs ?? []).flatMap((p) => {
          const t = block.outputs[p.index]
          return t ? [{ key: `out${p.index}`, name: p.pin.alias, text: `→ ${sourceLabel(ctx, { kind: 'variable', path: t.path })}` }] : []
        })
        return (
          <div key={block.id} className={classes} data-key={`block:${block.id}`} onClick={() => blocks.select(block.id)}>
            <div className="block-entry-head">
              <span className="block-entry-order" title="Runs as this block of the pass">{runs.has(block.id) ? runs.get(block.id)! + 1 : ''}</span>
              <span className="block-entry-title">{block.name?.trim() || headline(ctx, block).map((part, i) => (part.value ? <em key={i}>{part.text}</em> : <span key={i}>{part.text}</span>))}</span>
              <span className="block-entry-type">{type?.alias ?? block.type}</span>
              <TreeAction label={`Remove ${blocks.nameOf(block.id)}`} onClick={() => blocks.remove([block.id])} />
            </div>
            {(reads.length > 0 || writes.length > 0 || block.enables.length > 0) && (
              <div className="block-entry-pins">
                {[...reads, ...writes].map((r) => <span key={r.key}>{r.name} <b>{r.text}</b></span>)}
                {block.enables.length > 0 && <span>Run when <b>{block.enables.map((s) => sourceLabel(ctx, s)).join(block.enableMode === 'all' ? ' and ' : ' or ')}</b></span>}
              </div>
            )}
            {problems.map((d, i) => <FieldError key={i}>{d.message}</FieldError>)}
          </div>
        )
      })}
    </div>
  )
}
