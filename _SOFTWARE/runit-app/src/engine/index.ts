/*
 * The VM engine: variables, accessors, blocks, wiring and compilation of a project into the frames the board loads.
 * Modules (each has its own index; dependencies point down this list):
 *
 *   wire       the wire format: records, frames, arena, load checks
 *   catalog    the block palette, read from generated/blocks
 *   model      the project: variables, paths, block instances, pins
 *   graph      execution order, gates and loops
 *   compiler   project -> program, accessors, block state, codecs
 *   generated  the one place that reads generated/ (wire numbers, descriptors)
 *
 * `createEngine()` wires the bundled descriptors to all of it; `Engine` takes your own.
 */
export { createEngine } from './create'
export { Engine } from './engine'
export type { EngineDefinitions, EngineOptions } from './engine'

export * from './catalog'
export * from './compiler'
export * from './graph'
export * from './model'
export * from './wire'
export { loadGeneratedInput, loadWireDefinitions, wireDefinitions } from './generated'
