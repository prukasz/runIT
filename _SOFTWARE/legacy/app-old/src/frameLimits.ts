import { runitStreamCatalog } from './domain/descriptors'

/** Longest command the board takes: its frame limit (CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX) less the seq byte. */
const BOARD_MAX_FRAME_BYTES = runitStreamCatalog().board.limits.frameMax - 1
/** Longest value one BLE write carries (ATT, Bluetooth Core spec), a link limit rather than the board's. */
const ATT_MAX_VALUE = 512
export const MAX_FRAME_BYTES = Math.min(BOARD_MAX_FRAME_BYTES, ATT_MAX_VALUE)
/** Web Bluetooth hides the negotiated MTU; 128 goes through as a long write on any link. */
export const DEFAULT_MAX_FRAME_BYTES = Math.min(128, MAX_FRAME_BYTES)
