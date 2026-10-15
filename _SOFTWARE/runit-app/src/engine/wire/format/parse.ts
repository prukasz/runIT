import type { WireDefinitions } from './definitions'
import type { AccessorDef, BlockDef, DataDef, ObjectDef, ProgramIR } from './program'
import { OBJ_WIDTH } from './types'
import type { IndexStep, ObjType } from './types'

/*
 * The inverse of records.ts and frames.ts: the load frames of a program (as the board stores them, or as they were sent) back
 * into the program they describe. Every record is read the way the board reads it (`vm_wire.h`); a record that does not fit
 * its frame stops the frame and is reported, the others are kept.
 */

export interface WireFrame {
  readonly op: number
  readonly payload: Uint8Array
}

export interface ParsedProgram {
  readonly ir: ProgramIR
  /** The slot counts OPEN announced (objects, accessors, blocks) and the arena it asked for. */
  readonly open?: { readonly objects: number; readonly accessors: number; readonly blocks: number; readonly arena: number }
  /** The EXEC commands that followed the load, by name (`normal` starts the program). */
  readonly exec: readonly string[]
  /** Frames that are not part of a load (SUBSCRIBE, SPEED, RESET) and anything that did not parse: said in words. */
  readonly problems: readonly string[]
}

const utf8 = new TextDecoder()

/** The program in these frames. Frames of the load are taken in order; later records of the same id replace earlier ones. */
export const parseProgram = (defs: WireDefinitions, frames: readonly WireFrame[]): ParsedProgram => {
  const objects = new Map<number, ObjectDef>()
  const data: DataDef[] = []
  const accessors = new Map<number, AccessorDef>()
  const blocks = new Map<number, BlockDef>()
  const exec: string[] = []
  const problems: string[] = []
  let open: ParsedProgram['open']

  const types = new Map<number, ObjType>(Object.entries(defs.objTypes).map(([name, number]) => [number, name as ObjType]))
  const execNames = new Map(Object.entries(defs.exec).map(([name, number]) => [number, name]))
  const enMode = new Map(Object.entries(defs.enMode).map(([name, number]) => [number, name as 'any' | 'all']))
  const onError = new Map(Object.entries(defs.onError).map(([name, number]) => [number, name as 'stop' | 'continue']))
  const kinds = new Map(Object.entries(defs.idxKinds).map(([name, number]) => [number, name as 'literal' | 'ref' | 'name']))

  frames.forEach((frame, n) => {
    const where = `frame ${n + 1}`
    const view = new DataView(frame.payload.buffer, frame.payload.byteOffset, frame.payload.byteLength)
    const bytes = frame.payload
    const fail = (message: string) => problems.push(`${where}: ${message}`)

    if (frame.op === defs.ops.open) {
      if (bytes.length < 10) return fail('OPEN is cut short.')
      open = { objects: view.getUint16(0, true), accessors: view.getUint16(2, true), blocks: view.getUint16(4, true), arena: view.getUint32(6, true) }
    } else if (frame.op === defs.ops.addObjects) {
      let at = 1
      for (let i = 0; i < (bytes[0] ?? 0); i++) {
        if (at + 6 > bytes.length) return fail('an object record is cut short.')
        const id = view.getUint16(at, true)
        const payload = view.getUint16(at + 2, true)
        const typeAndName = bytes[at + 4]!
        const flags = bytes[at + 5]!
        const type = types.get(typeAndName & 0x0f)
        const nameLength = typeAndName >> 4
        at += 6
        if (!type) return fail(`object ${id} has the unknown type ${typeAndName & 0x0f}.`)
        if (at + nameLength > bytes.length) return fail('an object name is cut short.')
        const count = Math.max(Math.floor(payload / OBJ_WIDTH[type].mem), 1)
        objects.set(id, {
          id,
          type,
          count,
          flags: {
            mutable: (flags & defs.headFlags.mutable) !== 0,
            updResetable: (flags & defs.headFlags.updResetable) !== 0,
            retentive: (flags & defs.headFlags.retentive) !== 0,
            usrProtected: (flags & defs.headFlags.usrProtected) !== 0,
          },
          name: utf8.decode(bytes.subarray(at, at + nameLength)),
        })
        at += nameLength
      }
    } else if (frame.op === defs.ops.setData) {
      let at = 1
      for (let i = 0; i < (bytes[0] ?? 0); i++) {
        if (at + 6 > bytes.length) return fail('a value record is cut short.')
        const id = view.getUint16(at, true)
        const start = view.getUint16(at + 2, true)
        const length = view.getUint16(at + 4, true)
        at += 6
        if (at + length > bytes.length) return fail('a value is cut short.')
        data.push({ id, start, data: bytes.slice(at, at + length) })
        at += length
      }
    } else if (frame.op === defs.ops.addAccessors) {
      let at = 1
      for (let i = 0; i < (bytes[0] ?? 0); i++) {
        if (at + 6 > bytes.length) return fail('an accessor record is cut short.')
        const id = view.getUint16(at, true)
        const root = view.getUint16(at + 2, true)
        const stepCount = bytes[at + 4]!
        const stepBytes = bytes[at + 5]!
        at += 6
        const end = at + stepBytes
        if (end > bytes.length) return fail('accessor steps are cut short.')
        const steps: IndexStep[] = []
        for (let s = 0; s < stepCount; s++) {
          const kind = kinds.get(bytes[at]!)
          at += 1
          if (kind === 'literal') {
            steps.push({ kind, value: view.getUint32(at, true) })
            at += 4
          } else if (kind === 'ref') {
            steps.push({ kind, accessor: view.getUint16(at, true) })
            at += 2
          } else if (kind === 'name') {
            const length = bytes[at]!
            steps.push({ kind, name: utf8.decode(bytes.subarray(at + 1, at + 1 + length)) })
            at += 1 + length
          } else return fail(`accessor ${id} has an unknown step kind.`)
        }
        if (at !== end) return fail(`accessor ${id}: its steps are not the length it says.`)
        accessors.set(id, { id, root, steps })
      }
    } else if (frame.op === defs.ops.addBlock) {
      if (bytes.length < 14) return fail('a block record is cut short.')
      const id = view.getUint16(0, true)
      const index = view.getUint16(2, true)
      const type = bytes[4]!
      const inCount = bytes[5]!
      const outCount = bytes[6]!
      const enCount = bytes[7]!
      const stateLength = view.getUint16(10, true)
      const eno = view.getUint16(12, true)
      const ids = (from: number, count: number) => Array.from({ length: count }, (_, k) => view.getUint16(from + k * 2, true))
      const idsAt = 14
      if (idsAt + (inCount + outCount + enCount) * 2 + stateLength > bytes.length) return fail(`block ${id} is cut short.`)
      const stateAt = idsAt + (inCount + outCount + enCount) * 2
      blocks.set(id, {
        id,
        index,
        type,
        inputs: ids(idsAt, inCount),
        outputs: ids(idsAt + inCount * 2, outCount),
        enables: ids(idsAt + (inCount + outCount) * 2, enCount),
        enableMode: enMode.get(bytes[8]!) ?? 'any',
        onError: onError.get(bytes[9]!) ?? 'continue',
        state: bytes.slice(stateAt, stateAt + stateLength),
        eno,
      })
    } else if (frame.op === defs.ops.exec) {
      exec.push(execNames.get(bytes[0] ?? -1) ?? `unknown ${bytes[0]}`)
    } else {
      fail(`op ${frame.op} is not part of a load.`)
    }
  })

  const byId = <T extends { readonly id: number }>(map: Map<number, T>): T[] => [...map.values()].sort((a, b) => a.id - b.id)
  return { ir: { objects: byId(objects), data, accessors: byId(accessors), blocks: byId(blocks) }, ...(open ? { open } : {}), exec, problems }
}
