/*
 * The backend: the link to a runIT board. No UI. Modules (dependencies point down this list):
 *
 *   definitions   the link's numbers and the error tags, read from generated/common
 *   protocol      the router's request / response frames and the error record
 *   transport     BLE: the platform interface, framing, Web Bluetooth
 *   session       the commands and the link that keeps itself connected
 */
export { bundledLinkDefinitions, errorCatalog, linkDefinitions } from './definitions'
export type { ErrorField, ErrorInfo, LinkDefinitions } from './definitions'
export { decodeErrorRecord } from './protocol/errors'
export type { ErrorNode, ErrorRecord } from './protocol/errors'
export { decodeResponse, encodeRequest } from './protocol/frames'
export type { Request, Response } from './protocol/frames'
export { fragment, Reassembler } from './transport/framing'
export type { Reassembled } from './transport/framing'
export type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers } from './transport/types'
export { shortUuid, WebBluetoothPlatform } from './transport/webBluetooth'
export { CommandClient, CommandError } from './session/commands'
export type { CommandClientOptions, CommandErrorCode, CommandEvent, CommandOptions } from './session/commands'
export { ATT_VALUE_MAX, DEFAULT_RECONNECT, RunitLink, STREAM_NAMES } from './session/link'
export type { LinkDiagnostic, LinkOptions, LinkState, LinkStatus, ReconnectPolicy, StreamFrame, StreamName } from './session/link'
