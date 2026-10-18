import { Link2 } from 'lucide-react'
import { findVariable } from '@engine'
import type { Variable } from '@engine'
import { RefPill, TypeLead, leadOf } from '@ui/components'
import { useErrors } from '../console/useConsole'
import type { CodeState } from './useCode'
import { pathOf } from './variables'

/** A variable's lead square: the kind, and a single boolean's initial value (a click flips it). */
export function VariableLead({ variable, code }: { variable: Variable; code: CodeState }) {
  if (variable.kind !== 'value') return <TypeLead kind={variable.kind} />
  if (variable.type !== 'bool' || variable.count !== 1) return <TypeLead kind={leadOf(variable.type)} />
  const on = variable.initial === true || (Array.isArray(variable.initial) && variable.initial[0] === true)
  return <TypeLead kind="bool" value={on} onToggle={() => code.update(variable.id, { initial: !on })} />
}

/** The variables the board's unread errors are about, with how many (the program on the board read back to them: `programRefs`). */
export function useVariableErrors(): ReadonlyMap<string, number> {
  const errors = useErrors()
  const counts = new Map<string, number>()
  for (const entry of errors?.console.entries ?? []) {
    if (entry.read) continue
    const ids = new Set(entry.report.refs.map((r) => errors!.console.interpreter.target(r))
      .flatMap((t) => (typeof t === 'object' && t !== null && (t as { kind?: unknown }).kind === 'variable' ? [String((t as { variable: unknown }).variable)] : [])))
    for (const id of ids) counts.set(id, (counts.get(id) ?? 0) + 1)
  }
  return counts
}

/** What a reference points at, as a pill that jumps there; dashed while unset, red when the target is gone. */
export function TargetPill({ variable, code }: { variable: Variable; code: CodeState }) {
  if (variable.kind !== 'reference') return null
  if (variable.target === null) return <RefPill compact empty icon={Link2} name="Choose…" onJump={() => code.select(variable.id)} />
  const target = findVariable(code.tree, variable.target)
  if (!target) return <RefPill compact broken name="Missing" />
  return <RefPill compact icon={Link2} name={pathOf(code.tree, target.id) ?? target.name} onJump={() => code.select(target.id)} />
}
