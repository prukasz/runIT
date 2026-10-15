import { encodeValue, TelemetryReader, writeFrames } from '../wire'
import type { DataDef, LoadFrame, ObjType, Value, VmLimits, WireDefinitions } from '../wire'
import type { CompileResult } from './compile'

/*
 * A compiled program at run time, by the user's variables: writing one (the remote control) and reading what the board sends
 * about them (telemetry). The object ids are the compile result's (`ids.variables`); the types are the program's.
 */

export interface VariableWrite {
  /** The variable's id. */
  readonly variable: string
  /** One element, a list, or raw bytes. */
  readonly value: Value
  /** The first element written (default 0): to write one element of an array. */
  readonly element?: number
}

const typesOf = (result: CompileResult): Map<number, ObjType> => new Map((result.ir?.objects ?? []).map((o) => [o.id, o.type]))

/** The frames that write these variables on the running program (applied at the start of the next pass). */
export const variableWriteFrames = (defs: WireDefinitions, result: CompileResult, writes: readonly VariableWrite[], limits?: VmLimits): LoadFrame[] => {
  const types = typesOf(result)
  const data: DataDef[] = writes.map((w) => {
    const id = result.ids.variables.get(w.variable)
    if (id === undefined) throw new RangeError(`'${w.variable}' is not a variable of the program.`)
    return { id, start: w.element ?? 0, data: encodeValue(types.get(id)!, w.value) }
  })
  return writeFrames(defs, data, types, limits)
}

/** A reader of the telemetry of this program: its values come with the user's variable they belong to. */
export const variableTelemetryReader = (defs: WireDefinitions, result: CompileResult): TelemetryReader =>
  new TelemetryReader(defs, typesOf(result), new Map([...result.ids.variables].map(([variable, id]) => [id, variable])))
