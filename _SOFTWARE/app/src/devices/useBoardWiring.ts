import { useState } from 'react'
import { usePersistedFlag } from '../hooks/useStorage'

/*
 * The Board canvas's wiring view options (board-canvas-concept.md §6) and the selected wire. Shared by the toolbar that
 * switches them, the canvas that draws with them and the details panel. The options are remembered; the selection is not.
 */
export interface BoardWiring {
  /** Show power and ground wires normally (off: faded). */
  readonly power: boolean
  /** Show signal and bus wires normally (off: faded). */
  readonly io: boolean
  /** Fade every wire that is not on the selected device. */
  readonly focus: boolean
  /** Label wires with their net's voltage. */
  readonly voltages: boolean
  /** While a wire is dragged, dim the canvas and ring only the pins that can take it. */
  readonly availableOnly: boolean
  readonly toggle: (option: 'power' | 'io' | 'focus' | 'voltages' | 'availableOnly') => void
  readonly selectedWire: string | undefined
  readonly selectWire: (id: string | undefined) => void
}

export function useBoardWiring(): BoardWiring {
  const [power, setPower] = usePersistedFlag('runit.board.wires.power', true)
  const [io, setIo] = usePersistedFlag('runit.board.wires.io', true)
  const [focus, setFocus] = usePersistedFlag('runit.board.wires.focus', false)
  const [voltages, setVoltages] = usePersistedFlag('runit.board.wires.voltages', true)
  const [availableOnly, setAvailableOnly] = usePersistedFlag('runit.board.wires.available', true)
  const [selectedWire, selectWire] = useState<string>()
  const current = { power, io, focus, voltages, availableOnly }
  const setters = { power: setPower, io: setIo, focus: setFocus, voltages: setVoltages, availableOnly: setAvailableOnly }
  return { ...current, toggle: (option) => setters[option](!current[option]), selectedWire, selectWire }
}
