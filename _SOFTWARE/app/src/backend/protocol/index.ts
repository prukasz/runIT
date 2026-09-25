export { CommandClient, CommandError, describeError } from './CommandClient'
export type {
  CommandClientOptions,
  CommandErrorCode,
  CommandEvent,
  CommandEventHandler,
  CommandFrameSource,
  CommandRequest,
  CommandResponse,
  CommandSender,
} from './CommandClient'
export { decodeInterfaceResponse, encodeCommandFrame, RUNIT_ROUTE } from './frames'
export type { InterfaceErrorData, InterfaceProtocol, InterfaceResponseFrame } from './frames'
