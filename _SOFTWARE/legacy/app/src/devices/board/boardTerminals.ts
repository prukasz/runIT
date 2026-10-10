import terminalTable from '@data-structures/board/board-terminals.json'
import type { SignalSpec } from '../../domain/board'
import type { NetSpec, PinDir, PinKind, PinSpec } from '../../domain/hardware'

/*
 * The runIT board's wirable terminals (data-structures/board/board-terminals.json, hand-written): what each terminal in
 * the art is electrically, and the named nets (GND, 3V3, REG1, REG2, VS) whose terminals are joined on the PCB.
 */

interface TerminalRow {
  readonly label: string
  readonly kind: PinKind
  readonly dir?: PinDir
  readonly modes?: readonly string[]
  readonly mV_min?: number
  readonly mV_max?: number
  readonly net?: string
  readonly signal?: string
  readonly pin?: { readonly device: number; readonly pin: number }
  readonly note?: string
}

const DEFAULT_DIR: Record<PinKind, PinDir> = { supply_out: 'out', supply_in: 'in', gnd: 'in', io: 'inout', bus: 'inout' }

export const BOARD_NETS: Readonly<Record<string, NetSpec>> = terminalTable.nets as Record<string, NetSpec>

/** The bus signals a connector pin can carry, and which way each flows. */
export const BOARD_SIGNALS: readonly SignalSpec[] = terminalTable.signals as SignalSpec[]

/** What the pins of a provider device type tolerate, by device type ID. */
export const BOARD_SYS_LIMITS: Readonly<Record<string, { readonly mV_max?: number; readonly note?: string }>> = terminalTable.sys_pin_limits

export const BOARD_TERMINALS: ReadonlyMap<string, PinSpec> = new Map(
  Object.entries(terminalTable.terminals as Record<string, TerminalRow>).map(([id, row]) => [
    id,
    {
      kind: row.kind,
      dir: row.dir ?? DEFAULT_DIR[row.kind],
      label: row.label,
      modes: row.modes ?? [],
      ...(row.mV_min !== undefined ? { mV_min: row.mV_min } : {}),
      ...(row.mV_max !== undefined ? { mV_max: row.mV_max } : {}),
      ...(row.net ? { net: row.net } : {}),
      ...(row.signal ? { signal: row.signal } : {}),
      ...(row.pin ? { sysPin: row.pin } : {}),
      ...(row.note ? { note: row.note } : {}),
    } satisfies PinSpec,
  ]),
)
