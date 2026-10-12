import { GRID } from '../../domain/canvas'
import { ART_SCALE, scopeSvg } from './boardArt'
import runitBoardSvg from './runit-board.svg?raw'

/** The runIT board template (phase 1: the one board), scoped for inlining, and its width in millimetres (grid cells). */
export const BOARD_SCOPE = 'board-art'
export const RUNIT_BOARD = scopeSvg(runitBoardSvg, BOARD_SCOPE)
export const RUNIT_BOARD_WIDTH_MM = (RUNIT_BOARD.width * ART_SCALE) / GRID
