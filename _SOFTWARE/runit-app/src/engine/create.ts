import { Engine } from './engine'
import type { EngineOptions } from './engine'
import { loadGeneratedInput, loadLimits, loadWireDefinitions } from './generated'
import type { VmLimits } from './wire/load/limits'

/**
 * An engine over the firmware descriptors bundled in generated/ (read by Vite at build time): its blocks, its wire numbers
 * and its limits. Pass `codecs` to extend it, `limits` to override the published ones (a board built with another Kconfig);
 * the frame size is not one of them: it is the same on every link.
 */
export const createEngine = (options: Partial<Omit<EngineOptions, 'definitions' | 'limits'>> & { readonly limits?: Partial<Omit<VmLimits, 'frameMax'>> } = {}): Engine =>
  new Engine({
    definitions: { wire: loadWireDefinitions(), input: loadGeneratedInput() },
    ...options,
    limits: { ...loadLimits(), ...options.limits },
  })
