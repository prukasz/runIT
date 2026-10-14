import { Engine } from './engine'
import type { EngineOptions } from './engine'
import { loadGeneratedInput, loadLimits, loadWireDefinitions } from './generated'

/**
 * An engine over the firmware descriptors bundled in generated/ (read by Vite at build time): its blocks, its wire numbers
 * and its limits. Pass `codecs` to extend it, `limits` to override the published ones (a link that carries a smaller frame,
 * a board built with another Kconfig).
 */
export const createEngine = (options: Partial<Omit<EngineOptions, 'definitions'>> = {}): Engine =>
  new Engine({
    definitions: { wire: loadWireDefinitions(), input: loadGeneratedInput() },
    ...options,
    limits: { ...loadLimits(), ...options.limits },
  })
