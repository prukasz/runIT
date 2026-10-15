import { Catalog } from './catalog/catalog'
import type { GeneratedInput } from './catalog/fromGenerated'
import { codecRegistry } from './compiler/block/codecs'
import type { BlockCodec, CodecRegistry } from './compiler/block/codecs'
import { compile } from './compiler/compile'
import type { CompileOptions, CompileResult } from './compiler/compile'
import { expressionCodecs } from './compiler/block/expressionCodec'
import { expressionLanguage } from './expression'
import type { ExpressionLanguage } from './expression'
import { arrange } from './graph/arrange'
import type { Arrangement } from './graph/arrange'
import { concretePins } from './model/program/pins'
import type { ConcretePins } from './model/program/pins'
import type { BlockInstance, Project } from './model/program/program'
import type { WireDefinitions } from './wire/format/definitions'
import { execFrame, resetFrame, speedFrame, subscribeFrame } from './wire/load/frames'
import type { LoadFrame } from './wire/load/frames'
import { DEFAULT_LIMITS } from './wire/load/limits'
import type { VmLimits } from './wire/load/limits'
import type { ExecCommand } from './wire/format/types'

/**
 * Everything an app needs to build on the VM, in one object: the block catalog, the wire numbers, the codecs and limits it
 * works with, and the operations on a project (compile, arrange, a block's pins) and on the running VM (control frames).
 * It holds no project and no connection: it is data in, data out, so any number of projects can use one engine.
 *
 * Pure: it never reads a file. `createEngine` (create.ts) is the one that loads the bundled firmware descriptors; build an
 * `Engine` directly to give your own (a test, another firmware).
 */

export interface EngineDefinitions {
  readonly wire: WireDefinitions
  /** The descriptors the catalog is built from (generated/blocks, contracts, devices and the enums). */
  readonly input: GeneratedInput
}

export interface EngineOptions {
  readonly definitions: EngineDefinitions
  /** What the board refuses (the firmware's Kconfig); defaults are the shipped configuration. */
  readonly limits?: Partial<VmLimits>
  /** Codecs for block editors, beyond the ones the engine ships with. */
  readonly codecs?: readonly BlockCodec[]
}

export class Engine {
  readonly catalog: Catalog
  readonly wire: WireDefinitions
  readonly limits: VmLimits
  readonly codecs: CodecRegistry
  private readonly enums: GeneratedInput['enums']
  private readonly languages = new Map<string, ExpressionLanguage | undefined>()

  constructor(options: EngineOptions) {
    this.wire = options.definitions.wire
    this.enums = options.definitions.input.enums
    this.catalog = Catalog.fromGenerated(options.definitions.input)
    this.limits = { ...DEFAULT_LIMITS, ...options.limits }
    // the formula codecs come from the firmware's opcode enums; a codec given for the same editor replaces them
    this.codecs = codecRegistry(...expressionCodecs(this.catalog, options.definitions.input.enums), ...(options.codecs ?? []))
  }

  /** The project as the frames that load it, with every problem found. */
  compile(project: Project, options?: CompileOptions): CompileResult {
    return compile({ catalog: this.catalog, wire: this.wire, codecs: this.codecs, limits: this.limits }, project, options)
  }

  /**
   * The formula language of a block type (`expr`, `expr_bit`): its operators and functions, the text <-> RPN conversion, all on
   * the opcodes of that block's machine. Undefined for a block that is not a formula block.
   */
  expression(blockType: string): ExpressionLanguage | undefined {
    const type = this.catalog.get(blockType)
    if (!type) return undefined
    if (!this.languages.has(type.id)) this.languages.set(type.id, expressionLanguage(this.enums, type))
    return this.languages.get(type.id)
  }

  /** The order the project's blocks run in, and the span of each loop. */
  arrange(project: Project): Arrangement {
    return arrange(this.catalog, project, this.limits)
  }

  /** A block's concrete pins (groups expanded to the count the block has): the positions its wiring uses. */
  pins(instance: BlockInstance): ConcretePins | undefined {
    const type = this.catalog.get(instance.type)
    return type ? concretePins(type, instance) : undefined
  }

  /* ---- the running VM ---- */

  reset(): LoadFrame {
    return resetFrame(this.wire)
  }

  exec(command: ExecCommand): LoadFrame {
    return execFrame(this.wire, command)
  }

  speed(factor: number): LoadFrame {
    return speedFrame(this.wire, factor)
  }

  /** Telemetry for these objects (the ids `compile` returns); none clears. */
  subscribe(objectIds: readonly number[]): LoadFrame {
    return subscribeFrame(this.wire, objectIds, this.limits)
  }
}
