import { useMemo } from 'react'
import { boardTerminals, pinLabel, renderBoardSvg, connectorType } from '../../domain/board'
import type { BoardConnector, BoardTemplate, PinBinding, SignalSpec } from '../../domain/board'
import type { DeviceCatalog } from '../../domain/descriptors'
import type { NetSpec, PinSpec } from '../../domain/hardware'
import { ART_SCALE, scopeSvg } from './boardArt'
import type { BoardArt } from './boardArt'
import { BOARD_NETS, BOARD_SIGNALS, BOARD_TERMINALS } from './boardTerminals'
import { BOARD_SCOPE, RUNIT_BOARD, RUNIT_BOARD_WIDTH_MM } from './runitBoard'
import { sysPinSpec } from './sysPins'
import { GRID } from '../../domain/canvas'

/*
 * The board the canvas draws and the wiring reads, whichever it is: the built-in runIT board (hand-drawn art, its
 * terminal table) or a board template made in the board creator (art generated from its connectors, terminals from their
 * pin bindings). Everything downstream asks the model for the art, its size and its terminals.
 */

export interface BoardModel {
  readonly id: string
  readonly name: string
  /** The built-in runIT board: its art is fixed, it has no template to edit. */
  readonly builtin: boolean
  readonly art: BoardArt
  readonly widthMm: number
  readonly heightMm: number
  /** The wirable terminals by their art ID. */
  readonly terminals: ReadonlyMap<string, PinSpec>
  readonly nets: Readonly<Record<string, NetSpec>>
  /** The bus signals a connector pin can carry. */
  readonly signals: readonly SignalSpec[]
  readonly template?: BoardTemplate
}

const mm = (units: number): number => (units * ART_SCALE) / GRID

export const RUNIT_MODEL: BoardModel = {
  id: 'runit',
  name: 'runIT',
  builtin: true,
  art: RUNIT_BOARD,
  widthMm: RUNIT_BOARD_WIDTH_MM,
  heightMm: mm(RUNIT_BOARD.height),
  terminals: BOARD_TERMINALS,
  nets: BOARD_NETS,
  signals: BOARD_SIGNALS,
}

const signalLabel = (id: string): string => BOARD_SIGNALS.find((signal) => signal.id === id)?.label ?? id

/** What a binding says in words, for a terminal's tooltip. */
export function describeBinding(binding: PinBinding, catalog: DeviceCatalog, nets: Readonly<Record<string, NetSpec>>): string {
  switch (binding.type) {
    case 'none':
      return 'not connected'
    case 'net':
      return `${nets[binding.net]?.label ?? binding.net}${binding.role === 'input' ? ' (board input)' : ''}`
    case 'signal':
      return signalLabel(binding.signal)
    case 'sys': {
      const spec = sysPinSpec(catalog, { device: binding.device, pin: binding.pin })
      return spec ? spec.label : `unknown sys pin ${binding.device}.${binding.pin}`
    }
  }
}

export function buildBoardModel(template: BoardTemplate, catalog: DeviceCatalog, nets: Readonly<Record<string, NetSpec>> = BOARD_NETS): BoardModel {
  const describe = (connector: BoardConnector, index: number): string => `${pinLabel(connector, index)} · ${describeBinding(connector.pins[index]?.bind ?? { type: 'none' }, catalog, nets)}`
  const art = scopeSvg(renderBoardSvg(template, describe), BOARD_SCOPE)
  return {
    id: template.id,
    name: template.name,
    builtin: false,
    art,
    widthMm: template.width,
    heightMm: template.height,
    terminals: boardTerminals(template, { nets, signals: BOARD_SIGNALS, sysPin: (ref) => sysPinSpec(catalog, ref) }),
    nets,
    signals: BOARD_SIGNALS,
    template,
  }
}

/** The board of a project: its template, or the built-in runIT board. */
export const useBoardModel = (template: BoardTemplate | undefined, catalog: DeviceCatalog): BoardModel =>
  useMemo(() => (template ? buildBoardModel(template, catalog) : RUNIT_MODEL), [template, catalog])

export { connectorType }
