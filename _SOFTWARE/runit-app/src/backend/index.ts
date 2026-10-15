/*
 * The backend: the link to a runIT board. No UI. Modules (dependencies point down this list):
 *
 *   definitions   the link's numbers, the error tags and the descriptors, read from generated/
 *   catalog       every command of every router as the descriptors describe it, and its payload layout
 *   protocol      the router's request / response frames, the error record, the request of any catalog command
 *   transport     BLE: the platform interface, framing, Web Bluetooth
 *   session       the commands, the link that keeps itself connected, sending a list in order, the catalog's commands
 */
export { bundledLinkDefinitions, bundledServiceCatalog, errorCatalog, linkDefinitions } from './definitions'
export type { ErrorField, ErrorInfo, LinkDefinitions } from './definitions'
export { decodeFields, encodeFields, PayloadError, RAW_TYPES, rawWidth, readValue } from './catalog/codec'
export type { Choice, FieldInput, FieldRecord, FieldValue, FieldValues, Layout, RawType, WireField } from './catalog/codec'
export { ServiceCatalog } from './catalog/services'
export type { CatalogEntry, CommandDef, DescriptorFile, EntryKind, EventDef, Returns } from './catalog/services'
export { createDeviceCall, decodeAnswer, decodeReturns, deviceCall, deviceOp, requestFrame, serviceCall } from './protocol/services'
export type { Decoded, ServiceCall } from './protocol/services'
export { Services } from './session/services'
export type { Answer } from './session/services'
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
export { sendInOrder, sendProgram } from './session/send'
export type { ProgramFrame, SendOptions } from './session/send'
export {
  crc32, deviceFrame, deviceRecordId, frameImage, listRecords, parseImage, programFrameOf, PROJECT_RECORD_ID, projectFrames, projectInfo,
  projectStream, readProject, readRecord, recordInfo, removeRecord, runProject, runRecord, saveProject, SECTION_PROGRAM, setAutostart, storeRecord,
  eraseProject, vmFrame,
} from './session/project'
export type { ProjectContent, ProjectInfo, RecordInfo, SaveOptions, Section, StoredProject } from './session/project'
