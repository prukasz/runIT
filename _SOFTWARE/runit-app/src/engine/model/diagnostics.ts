/**
 * Something the engine found wrong, or worth saying, about a project. A UI lists these next to what they name; `code`
 * is stable (the firmware's error tag where one exists, else a short name of the engine's own).
 */
export interface Diagnostic {
  readonly severity: 'error' | 'warning' | 'info'
  readonly code: string
  readonly message: string
  readonly where: {
    readonly kind: 'program' | 'variable' | 'block'
    readonly id?: string
    /** For a block: the pin the problem is about (`in`, `out` or `en`, and its position). */
    readonly pin?: { readonly side: 'in' | 'out' | 'en'; readonly index: number }
    /** For a block: the field the problem is about. */
    readonly field?: string
  }
}

export const hasErrors = (diagnostics: readonly Diagnostic[]): boolean => diagnostics.some((d) => d.severity === 'error')
