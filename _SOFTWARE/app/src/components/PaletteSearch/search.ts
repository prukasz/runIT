/*
 * Matching for the palette searches (variables, blocks): a name matches when it contains the query, ignoring case, spaces and
 * underscores. `searchScore` ranks the matches so a palette can highlight its best one (and Enter picks it).
 */

const tight = (text: string): string => text.replace(/\s+/g, '').toLowerCase()
const loose = (text: string): string => text.replace(/[\s_]+/g, '').toLowerCase()

export const matchesSearch = (name: string, query: string): boolean => {
  const q = query.trim()
  if (!q) return false
  const qNorm = tight(q)
  if (!qNorm) return false
  if (tight(name).includes(qNorm)) return true
  const qLoose = loose(q)
  return !!qLoose && loose(name).includes(qLoose)
}

/** 0: no match; then a match inside the name 1, at the start of a word 2, at the start of the name 3, the whole name 4. */
export const searchScore = (name: string, query: string): number => {
  if (!matchesSearch(name, query)) return 0
  const q = loose(query)
  const n = loose(name)
  if (n === q) return 4
  if (n.startsWith(q)) return 3
  const words = name.toLowerCase().split(/[\s_\-:]+/).filter(Boolean)
  return words.some((word) => word.startsWith(query.trim().toLowerCase())) ? 2 : 1
}

/** The best of `candidates` for `query` (the first of equals, in display order), or undefined when none matches. A candidate with
 * several names (an operation and its block) scores its best one. */
export const bestMatch = <T,>(candidates: readonly T[], label: (candidate: T) => string | readonly string[], query: string): T | undefined => {
  let best: T | undefined
  let bestScore = 0
  for (const candidate of candidates) {
    const names = label(candidate)
    const score = Math.max(...(typeof names === 'string' ? [names] : names).map((name) => searchScore(name, query)))
    if (score > bestScore) {
      best = candidate
      bestScore = score
    }
  }
  return best
}
