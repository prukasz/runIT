import { arrangeProgram } from '../canvas'
import { compileProgram, packSubscribe, subscribedWireIds } from '../compiler'
import type { CompiledProgram, Diagnostic } from '../compiler'
import type { VmCatalog } from '../descriptors'
import type { ObjectSection, ProgramBlock, ProjectDocument } from '../project'
import type { UploadPlan, UploadStep } from './bundle'

/*
 * Project (+ extra object sections) → the frames that load it: 0x41 open,
 * 0x42 objects, 0x43 values and folder links, 0x44 accessors, 0x45 the blocks
 * of the project's enabled canvases, then 0x47 subscribe to the objects
 * marked `subscribed` (always sent, so an empty list clears an old
 * subscription).
 */

export interface VmUploadOptions {
  readonly maxFrameBytes: number
  readonly sections?: readonly ObjectSection[]
  /** The program's blocks; default: the project's enabled canvases in order. */
  readonly blocks?: readonly ProgramBlock[]
}

export interface VmUploadPlan extends UploadPlan {
  readonly program: CompiledProgram
  /** Wire IDs in the subscribe packet. */
  readonly subscribed: readonly number[]
}

const PACKET_LABELS = (catalog: VmCatalog): ReadonlyMap<number, string> => new Map([
  [catalog.packets.open, 'vm open'],
  [catalog.packets.addObjects, 'vm add objects'],
  [catalog.packets.setData, 'vm set values'],
])

const labelFrames = (catalog: VmCatalog, frames: readonly Uint8Array[]): UploadStep[] => {
  const names = PACKET_LABELS(catalog)
  const totals = new Map<number, number>()
  frames.forEach((frame) => totals.set(frame[1]!, (totals.get(frame[1]!) ?? 0) + 1))
  const seen = new Map<number, number>()
  return frames.map((frame) => {
    const packet = frame[1]!
    const index = (seen.get(packet) ?? 0) + 1
    seen.set(packet, index)
    const total = totals.get(packet)!
    const name = names.get(packet) ?? `vm 0x${packet.toString(16)}`
    return { label: total > 1 ? `${name} ${index}/${total}` : name, frame }
  })
}

const toUploadDiagnostic = (entry: Diagnostic) => ({ severity: entry.severity, message: entry.message, ...(entry.objectId ? { subjectId: entry.objectId } : {}) })

export const planVmUpload = (project: ProjectDocument, catalog: VmCatalog, options: VmUploadOptions): VmUploadPlan => {
  const arranged = options.blocks ? undefined : arrangeProgram(project.canvases ?? [])
  const program = compileProgram(project, catalog, { ...options, blocks: options.blocks ?? arranged!.blocks })
  const found = [...(arranged?.diagnostics ?? []), ...program.diagnostics]
  if (arranged?.diagnostics.some((entry) => entry.severity === 'error')) return { ok: false, steps: [], diagnostics: found.map(toUploadDiagnostic), program, subscribed: [] }
  const diagnostics = found.map(toUploadDiagnostic)
  if (!program.ok) return { ok: false, steps: [], diagnostics, program, subscribed: [] }
  // No program, no frames: the board refuses to open an empty one (a project of devices only is fine).
  if (!program.counts.objects && !program.counts.blocks) return { ok: true, steps: [], diagnostics, program, subscribed: [] }

  const everything = [...project.objects, ...(options.sections ?? []).flatMap((section) => section.objects)]
  const subscribed = subscribedWireIds(everything, program.objects)
  const steps = labelFrames(catalog, program.frames)
  try {
    steps.push({ label: `vm subscribe (${subscribed.length})`, frame: packSubscribe(catalog, subscribed, options.maxFrameBytes) })
  } catch (error) {
    diagnostics.push({ severity: 'error', message: error instanceof Error ? error.message : String(error) })
    return { ok: false, steps: [], diagnostics, program, subscribed }
  }
  return { ok: true, steps, diagnostics, program, subscribed }
}
