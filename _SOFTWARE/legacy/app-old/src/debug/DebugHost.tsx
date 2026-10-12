import type { ReactNode } from 'react'
import type { RunitBleSession } from '../backend/runitBleSession'
import type { ObjectSection, ProjectDocument } from '../domain/project'
import { DebugProvider } from './DebugContext'
import { useDebugSession } from './useDebugSession'

/**
 * Owns the debug session and hands it to whatever below draws it. It is a
 * component of its own so a telemetry refresh re-renders the consumers of the
 * context, not the app around it (`children` are elements made by the parent).
 */
export function DebugHost({ project, sections, session, children }: {
  readonly project: ProjectDocument
  readonly sections: readonly ObjectSection[]
  readonly session: RunitBleSession | undefined
  readonly children: ReactNode
}) {
  return <DebugProvider value={useDebugSession(project, sections, session)}>{children}</DebugProvider>
}
