import { modulePort, parsePort, providerPort, sessionPort } from './routing'
import type { MatrixLink, ModulePortSpec, RoutingSnapshot } from './routing'

/*
 * The Connections window's model (pure): a provider is a card, and what it carries is a set of chips, one per module. A chip is
 * the links the module and the provider's data path need: the module's out to it, and its in from it, where the module has them
 * (`standardModulePorts`). On a provider with a session the data path is the session's sub-port of that module.
 */

/** Which side of a link: the endpoint a chip links its module to. */
export type Endpoint = { readonly kind: 'provider'; readonly provider: number } | { readonly kind: 'session'; readonly provider: number }

export const endpointPort = (module: number, endpoint: Endpoint): number =>
  endpoint.kind === 'session' ? sessionPort(endpoint.provider, module) : providerPort(endpoint.provider)

/** The links a module chip stands for: the module's out to the endpoint and the endpoint's out to the module's in. */
export const chipLinks = (spec: ModulePortSpec, endpoint: Endpoint): MatrixLink[] => {
  const port = endpointPort(spec.moduleId, endpoint)
  return [
    ...(spec.canOut ? [{ from: modulePort(spec.moduleId), to: port }] : []),
    ...(spec.canIn ? [{ from: port, to: modulePort(spec.moduleId) }] : []),
  ]
}

export type ChipState = 'on' | 'partial' | 'off'

const hasLink = (links: readonly MatrixLink[], l: MatrixLink): boolean => links.some((x) => x.from === l.from && x.to === l.to)

/** A chip is on when every link it stands for is there, partial when some are. */
export const chipState = (links: readonly MatrixLink[], expected: readonly MatrixLink[]): ChipState => {
  const have = expected.filter((l) => hasLink(links, l)).length
  return have === 0 ? 'off' : have === expected.length ? 'on' : 'partial'
}

/** The modules of a session whose sub-port the links show (a sub-port with no link is not known here). */
export const sessionModules = (links: readonly MatrixLink[], provider: number): number[] => {
  const found = new Set<number>()
  for (const l of links) {
    for (const port of [l.from, l.to]) {
      const p = parsePort(port)
      if (p.kind === 'session' && p.providerId === provider && p.moduleId !== 0) found.add(p.moduleId)
    }
  }
  return [...found].sort((a, b) => a - b)
}

/** The links that end or start at a sub-port of the provider's session: what a restart of the session must make again. */
export const subPortLinks = (links: readonly MatrixLink[], provider: number): MatrixLink[] =>
  links.filter((l) => [l.from, l.to].some((port) => {
    const p = parsePort(port)
    return p.kind === 'session' && p.providerId === provider && p.moduleId !== 0
  }))

/** The dead-man's choices, in ms (0: off); the session's own value is added when it is none of them. */
export const DEADMAN_CHOICES = [0, 1000, 3000, 10000, 30000, 60000] as const

export const deadmanLabel = (ms: number): string => (ms === 0 ? 'Off' : ms % 1000 === 0 ? `${ms / 1000} s` : `${ms} ms`)

export const deadmanChoices = (current: number): number[] => (DEADMAN_CHOICES.includes(current as never) ? [...DEADMAN_CHOICES] : [...DEADMAN_CHOICES, current].sort((a, b) => a - b))
