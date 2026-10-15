import { Catalog } from './catalog/catalog'
import type { GeneratedInput } from './catalog/fromGenerated'
import { codecRegistry } from './compiler/block/codecs'
import type { BlockCodec, CodecRegistry } from './compiler/block/codecs'
import { compile } from './compiler/compile'
import type { CompileOptions, CompileResult } from './compiler/compile'
import { expressionCodecs } from './compiler/block/expressionCodec'
import { recoverProject } from './recover'
import type { Recovered } from './recover'
import { parseProgram } from './wire/format/parse'
import type { ParsedProgram, WireFrame } from './wire/format/parse'
import { loadFrames } from './wire/load/frames'
import { variableTelemetryReader, variableWriteFrames } from './compiler/runtime'
import type { VariableWrite } from './compiler/runtime'
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
import type { TelemetryReader } from './wire/load/runtime'
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

export interface RecoverResult extends Recovered {
  readonly parsed: ParsedProgram
  /** Compiling the recovered project gives the same program. */
  readonly exact: boolean
  /** What the compiler says of the recovered project (a program from an older build may not compile today). */
  readonly compileDiagnostics: CompileResult['diagnostics']
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

  /**
   * The project a stored program describes: the load frames as the board keeps them (`WireFrame`: op and payload; the frames of a
   * project's program section, without their router keys). `exact`: compiling the project gives this very program again.
   * What a program never held (descriptions, block and case names, positions on a canvas) is not in the result.
   */
  recover(frames: readonly WireFrame[]): RecoverResult {
    const parsed = parseProgram(this.wire, frames)
    const recovered = recoverProject({ catalog: this.catalog, language: (type) => this.expression(type.id) }, parsed.ir)
    const compiled = this.compile(recovered.project)
    const bytes = (list: readonly WireFrame[]) => list.map((f) => `${f.op}:${[...f.payload].join(',')}`).join(';')
    const exact = compiled.ir !== undefined && bytes(loadFrames(this.wire, compiled.ir, this.limits, parsed.open?.arena)) === bytes(loadFrames(this.wire, parsed.ir, this.limits, parsed.open?.arena))
    return { ...recovered, parsed, exact, compileDiagnostics: compiled.diagnostics }
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

  /** The frames that write these variables of a compiled, running program (the remote control). */
  write(result: CompileResult, writes: readonly VariableWrite[]): LoadFrame[] {
    return variableWriteFrames(this.wire, result, writes, this.limits)
  }

  /** A reader of this program's telemetry frames (the telemetry stream's, without the stream byte): values with their variable. */
  telemetry(result: CompileResult): TelemetryReader {
    return variableTelemetryReader(this.wire, result)
  }

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
