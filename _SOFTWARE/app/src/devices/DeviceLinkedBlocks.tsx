import { ArrowUpRight } from 'lucide-react'
import { runitVmCatalog } from '../domain/descriptors'
import { pinDisplayLabel } from '../domain/devices'
import type { ProjectCanvas } from '../domain/project'
import type { ResolvedDevice } from '../domain/devices'
import type { DevicesWorkspace } from './useDevicesWorkspace'

export function DeviceLinkedBlocks({ workspace: w, device, canvases, onOpenBlock, heading = false }: {
  workspace: DevicesWorkspace
  device: ResolvedDevice
  canvases: readonly ProjectCanvas[]
  onOpenBlock: (canvasId: string, blockId: string) => void
  heading?: boolean
}) {
  const catalog = runitVmCatalog()
  const links = canvases.flatMap((canvas) => canvas.blocks.flatMap((block) => {
    const type = catalog.block(block.type)
    if (!type) return []
    const fields = type.fields.filter((field) => field.idKind === 'device' && Number(block.settings?.[field.name] ?? 0) === device.deviceId)
    if (!fields.length) return []
    const pins = [...new Set(fields.flatMap((field) => type.fields
      .filter((pinField) => pinField.idKind === 'pin' && pinField.deviceField === field.name)
      .map((pinField) => Number(block.settings?.[pinField.name] ?? 0))))]
    return [{ canvas, block, title: block.name || type.title || block.type, pins }]
  }))

  return <section className="devices-linked-blocks">
    {heading && <h3>Linked blocks</h3>}
    {links.length ? <ul>{links.map(({ canvas, block, title, pins }) => <li key={`${canvas.id}:${block.id}`}>
      <button type="button" onClick={() => onOpenBlock(canvas.id, block.id)} title={`Go to ${title} on ${canvas.name}`}>
        <span className="devices-linked-block-text"><strong>{title}</strong><small>{canvas.name}{pins.length ? ` · ${pins.map((pin) => pinDisplayLabel(w.catalog, device.ref, pin)).join(', ')}` : ''}{canvas.disabled ? ' · disabled canvas' : ''}</small></span>
        <ArrowUpRight aria-hidden="true" />
      </button>
    </li>)}</ul> : <p className="devices-muted">No blocks use this device.</p>}
  </section>
}
