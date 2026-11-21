/*
 * The backend: the link to a runIT board. No UI. Modules (dependencies point down this list):
 *
 *   definitions   the link's numbers, the error tags and the descriptors, read from generated/
 *   catalog       every command of every router, device type and provider type as the descriptors describe it, and its layout
 *   protocol      the frame (the header of every frame, requests, answers, pushes, the session's control frames), the request of
 *                 any catalog command and its answer, the error record, the events, the watches' entries, the GATT table and ways
 *   errors        an error record read for a user: messages from errors.json, the instances it is about, the app's names
 *   transport     BLE: the platform interface, framing, Web Bluetooth
 *   session       the commands, the link that keeps itself connected, the catalog's commands, the board's sessions, the watches,
 *                 the dead-man, the records, the errors kept for a user
 */
export { bundledBlockTypes, bundledEnums, bundledLinkDefinitions, bundledServiceCatalog, errorCatalog, errorIdKinds, errorSeverities, linkDefinitions, onboardActionIds, onboardActions, STREAM_MODULES, STREAM_NAMES } from './definitions'
export type { ErrorField, ErrorInfo, IdKindInfo, LinkDefinitions, OnboardAction, StreamName } from './definitions'
export { decodeFields, encodeFields, parseRanges, PayloadError, PIN_REF_SIZE, RAW_TYPES, rangesForConfig, rawWidth, readValue, withRanges } from './catalog/codec'
export type { Choice, FieldInput, FieldRanges, FieldRecord, FieldValue, FieldValues, Layout, PinRef, RawType, WireField } from './catalog/codec'
export { isMirror, ServiceCatalog, SIDES, sideOf, sidesOf } from './catalog/services'
export type { CatalogEntry, CommandDef, CommandSide, DeviceChannels, DevicePins, DevicePreset, DescriptorFile, EntryKind, EventDef, Returns, ReturnsLead, Side } from './catalog/services'
export { decodeFrame, decodeHello, encodeControl, encodeFrame, encodeHello, encodeRequest, frameRequest, HELLO_BYTES, requestFrame } from './protocol/frame'
export type { Frame, FrameHeader, Hello, Request } from './protocol/frame'
export { createDeviceCall, createProviderCall, createRemoteDeviceCall, decodeAnswer, decodeReturns, deviceCall, deviceOp, PROVIDER_COMMANDS, readDeviceFrame, readProviderFrame, serviceCall } from './protocol/calls'
export type { CallArgs, Decoded, DeviceFrame, LeadAnswer, ProviderFrame, ServiceCall } from './protocol/calls'
export { BLE_WAYS, bleTableCall, bleWayCall, decodeBleTable, readBleWay } from './protocol/gatt'
export type { BleWay, LiveCharacteristic, LiveGattService } from './protocol/gatt'
export { CLOCK_SET_AFTER_MS, decodeErrorRecord } from './protocol/errors'
export type { ErrorNode, ErrorRecord } from './protocol/errors'
export { decodeEvent, EVENT_LAYOUT } from './protocol/events'
export type { BoardEvent } from './protocol/events'
export { decodeWatchEntry, WATCH_COMMANDS, watchAddCall, watchRemoveCall } from './protocol/watches'
export type { WatchEntry } from './protocol/watches'
export { combineResolvers, ErrorInterpreter, refKey, refWithin } from './errors/interpret'
export type { ErrorReport, FieldReport, InterpreterOptions, NodeReport, Ref, RefResolver, RefTarget } from './errors/interpret'
export { deviceRefs, programRefs } from './errors/program'
export type { AppTarget } from './errors/program'
export { fragment, Reassembler } from './transport/framing'
export type { Reassembled } from './transport/framing'
export type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers, OpenSpec } from './transport/types'
export { shortUuid, WebBluetoothPlatform } from './transport/webBluetooth'
export { CommandClient, CommandError } from './session/commands'
export type { CommandClientOptions, CommandErrorCode, CommandEvent, CommandOptions } from './session/commands'
export { ATT_VALUE_MAX, DEFAULT_RECONNECT, RunitLink } from './session/link'
export type { LinkDiagnostic, LinkOptions, LinkState, LinkStatus, ReconnectPolicy, SessionInfo, StreamFrame } from './session/link'
export { Services } from './session/services'
export type { Answer, CallOptions } from './session/services'
export { decodePeers, peersCall, readPeers, SESSION_COMMANDS, Sessions } from './session/sessions'
export type { SessionPeer } from './session/sessions'
export { WATCH_HEAD, Watches } from './session/watches'
export type { FollowSpec, WatchHandler, WatchSpec } from './session/watches'
export { DEADMAN_COMMANDS, DeadMan } from './session/deadman'
export type { DeadManEvent, DeadManOptions } from './session/deadman'
export { ACTIONS_COMMANDS, crc32, frameImage, listRecords, parseImage, readRecord, recordInfo, removeRecord, runRecord, storeRecord } from './session/records'
export type { RecordInfo } from './session/records'
export { ErrorLog } from './session/errorlog'
export type { ErrorEntry, ErrorFilter, ErrorLogOptions } from './session/errorlog'
