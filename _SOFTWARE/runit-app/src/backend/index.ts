/*
 * The backend: the link to a runIT board. No UI. Modules (dependencies point down this list):
 *
 *   definitions   the link's numbers, the error tags and the descriptors, read from generated/
 *   catalog       every command of every router as the descriptors describe it, and its payload layout
 *   protocol      the session's frames (requests, answers, streams, HELLO), the error record, the request of any catalog command
 *   errors        an error record read for a user: messages from errors.json, the instances it is about, the app's names
 *   transport     BLE: the platform interface, framing, Web Bluetooth
 *   session       the commands, the link that keeps itself connected, sending a list in order, the catalog's commands
 */
export { appBoardKind, bundledBlockTypes, bundledBoard, bundledEnums, onboardActionIds, onboardActions, bundledLinkDefinitions, bundledServiceCatalog, errorCatalog, errorIdKinds, errorSeverities, linkDefinitions } from './definitions'
export type { BoardDescriptor, BoardDevice, BoardKind, BoardPin, BoardPower, PowerProbe, ErrorField, ErrorInfo, IdKindInfo, LinkDefinitions } from './definitions'
export { decodeFields, encodeFields, PayloadError, PIN_REF_SIZE, RAW_TYPES, rangesForConfig, rawWidth, readValue, withRanges } from './catalog/codec'
export type { Choice, FieldInput, FieldRanges, FieldRecord, FieldValue, FieldValues, Layout, PinRef, RawType, WireField } from './catalog/codec'
export { isMirror, ServiceCatalog, SIDES, sideOf, sidesOf } from './catalog/services'
export type { CatalogEntry, CommandDef, CommandSide, DeviceChannels, DevicePins, DevicePreset, DescriptorFile, EntryKind, EventDef, Returns, Side } from './catalog/services'
export { createDeviceCall, decodeAnswer, decodeReturns, deviceCall, deviceOp, readDeviceFrame, requestFrame, serviceCall } from './protocol/services'
export type { CallArgs, Decoded, DeviceFrame, ServiceCall } from './protocol/services'
export { Services } from './session/services'
export type { Answer, CallOptions } from './session/services'
export { DEADMAN_COMMANDS, DeadMan } from './session/deadman'
export type { DeadManEvent, DeadManOptions } from './session/deadman'
export { decodeErrorRecord, CLOCK_SET_AFTER_MS } from './protocol/errors'
export { decodeEvent, EVENT_LAYOUT } from './protocol/events'
export { decodeWatchEntry, watchAddCall, watchRemoveCall } from './protocol/watches'
export type { WatchEntry } from './protocol/watches'
export { Watches } from './session/watches'
export type { WatchHandler, WatchSpec } from './session/watches'
export type { BoardEvent } from './protocol/events'
export { combineResolvers, ErrorInterpreter, refKey, refWithin } from './errors/interpret'
export type { ErrorReport, FieldReport, InterpreterOptions, NodeReport, Ref, RefResolver, RefTarget } from './errors/interpret'
export { deviceRefs, programRefs } from './errors/program'
export type { AppTarget } from './errors/program'
export { ErrorLog } from './session/errorlog'
export type { ErrorEntry, ErrorFilter, ErrorLogOptions } from './session/errorlog'
export type { ErrorNode, ErrorRecord } from './protocol/errors'
export { decodeFrame, decodeHello, decodeResponse, encodeFrame, encodeHello, encodeRequest, HEADER_BYTES, HELLO_BYTES } from './protocol/frames'
export type { Frame, FrameHeader, Hello, Request, Response } from './protocol/frames'
export { fragment, Reassembler } from './transport/framing'
export type { Reassembled } from './transport/framing'
export type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers } from './transport/types'
export { shortUuid, WebBluetoothPlatform } from './transport/webBluetooth'
export { CommandClient, CommandError } from './session/commands'
export type { CommandClientOptions, CommandErrorCode, CommandEvent, CommandOptions } from './session/commands'
export { ATT_VALUE_MAX, DEFAULT_RECONNECT, RunitLink, STREAM_NAMES } from './session/link'
export type { LinkDiagnostic, LinkOptions, LinkState, LinkStatus, ReconnectPolicy, SessionInfo, StreamFrame, StreamName } from './session/link'
export { leaveSafeState, SAFE_STATE_COMMANDS } from './session/safestate'
export type { LeaveSafeStateOptions } from './session/safestate'
export { LOAD_COMMANDS, LoadError, loadProgram, sendInOrder, sendProgram } from './session/send'
export type { LoadOptions, ProgramFrame, SendOptions } from './session/send'
export {
  crc32, deviceFrame, deviceRecordId, frameImage, listRecords, parseImage, programFrameOf, PROJECT_RECORD_ID, projectFrames, projectInfo,
  projectStream, readProject, readRecord, recordInfo, removeRecord, runProject, runRecord, saveProject, SECTION_PROGRAM, SECTION_START, setProjectBoot, storeRecord,
  eraseProject, loadProject, projectBankRecordId, vmFrame,
} from './session/project'
export type { ProjectContent, ProjectInfo, RecordInfo, SaveOptions, Section, StoredProject } from './session/project'
