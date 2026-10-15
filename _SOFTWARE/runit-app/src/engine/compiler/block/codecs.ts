import type { BlockType } from '../../catalog/types'
import type { Diagnostic } from '../../model/program/diagnostics'
import type { ConcretePins } from '../../model/program/pins'
import type { BlockInstance } from '../../model/program/program'

/*
 * Codecs: what turns the part of a block that is not a plain field into the bytes of its state. A core block names its
 * editor in its descriptor (`editor`: `expression`, `switch-cases`); the codec of that name encodes what the editor
 * produced (`BlockInstance.editor`). The registry is how the engine is extended: a new editor is a new codec, registered
 * with the engine, nothing else changes.
 */

export interface CodecContext {
  readonly type: BlockType
  readonly instance: BlockInstance
  readonly pins: ConcretePins
  /** Report a problem with the block; `error` stops the program from being built. */
  readonly report: (severity: Diagnostic['severity'], code: string, message: string) => void
}

export interface BlockCodec {
  /** The editor id this codec serves (`BlockType.editor`). */
  readonly editor: string
  /** The block's state bytes, or null when it cannot be made (the codec has reported why). */
  encode(context: CodecContext): Uint8Array | null
}

export type CodecRegistry = ReadonlyMap<string, BlockCodec>

/** The cases of a switch are only names the user gives the outputs: the block has no state. */
export const switchCasesCodec: BlockCodec = {
  editor: 'switch-cases',
  encode: () => new Uint8Array(),
}

/** The registry of codecs the engine ships with, plus any given: a later one replaces an earlier one of the same editor. */
export const codecRegistry = (...more: readonly BlockCodec[]): CodecRegistry => new Map([switchCasesCodec, ...more].map((c) => [c.editor, c]))
