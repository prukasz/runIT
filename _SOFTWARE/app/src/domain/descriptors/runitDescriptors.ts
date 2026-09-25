import board from '@data-structures/board/board.generated.json'
import contracts from '@data-structures/contracts/contracts.generated.json'
import enums from '@data-structures/enums.json'
import errors from '@data-structures/errors/errors.generated.json'
import settings from '@data-structures/settings/settings.generated.json'
import streams from '@data-structures/streams/streams.generated.json'
import vmBlocks from '@data-structures/vm/blocks/index.generated.json'
import type { InterfaceProtocol } from '../../backend/protocol'
import { buildCommandCatalog, DescriptorError, parseByte } from './commandCatalog'
import type { CommandCatalog } from './commandCatalog'
import { buildErrorCatalog } from './errorCatalog'
import type { ErrorCatalog } from './errorCatalog'
import type { GeneratedBoardFile, GeneratedContractsFile, GeneratedEnumsFile, GeneratedErrorsFile, GeneratedSettingsFile, GeneratedStreamsFile, GeneratedVmBlocksIndex } from './generatedTypes'
import { buildStreamCatalog } from './streamCatalog'
import type { StreamCatalog } from './streamCatalog'
import { buildValueNames } from './valueNames'
import type { ValueNames } from './valueNames'

/*
 * The descriptors of the firmware this app was built against (data-structures/).
 * Each catalog is built once, on first use. Every firmware ID the app uses
 * (stream bytes, BLE UUIDs, class/packet bytes, status values, error tags and
 * owners) comes from here.
 */

let commandCatalog: CommandCatalog | undefined
let streamCatalog: StreamCatalog | undefined
let errorCatalog: ErrorCatalog | undefined
let interfaceProtocol: InterfaceProtocol | undefined
let valueNames: ValueNames | undefined
const enumsFile: GeneratedEnumsFile = enums

export const runitCommandCatalog = (): CommandCatalog => {
  commandCatalog ??= buildCommandCatalog(contracts satisfies GeneratedContractsFile, settings satisfies GeneratedSettingsFile)
  return commandCatalog
}

export const runitStreamCatalog = (): StreamCatalog => {
  streamCatalog ??= buildStreamCatalog(streams satisfies GeneratedStreamsFile)
  return streamCatalog
}

export const runitErrorCatalog = (): ErrorCatalog => {
  if (!errorCatalog) {
    const built = buildErrorCatalog(errors satisfies GeneratedErrorsFile)
    const stream = runitStreamCatalog().require('errors')
    if (built.stream !== stream.header) throw new DescriptorError(`Error packets name stream 0x${built.stream.toString(16)}, the errors connector sends 0x${stream.header.toString(16)}.`)
    errorCatalog = built
  }
  return errorCatalog
}

/** Response stream byte and OK status of the command envelope. */
export const runitInterfaceProtocol = (): InterfaceProtocol => {
  if (!interfaceProtocol) {
    const responseStream = parseByte(contracts.response_stream.class_header, 'response_stream')
    const stream = runitStreamCatalog().require('interface')
    if (responseStream !== stream.header) throw new DescriptorError(`Responses name stream 0x${responseStream.toString(16)}, the interface connector sends 0x${stream.header.toString(16)}.`)
    const statusEnum = enumsFile.enums[contracts.response_stream.status_enum]
    const ok = statusEnum?.members.find((member) => member.name.endsWith('_STATUS_OK'))
    if (!ok) throw new DescriptorError(`enums.json has no *_STATUS_OK member in ${contracts.response_stream.status_enum}.`)
    interfaceProtocol = { responseStream, statusOk: ok.value }
  }
  return interfaceProtocol
}

/** Names for annotated values (error payload fields): enums, board devices, error tags / owners / levels, commands, VM blocks, esp_err_t. */
export const runitValueNames = (): ValueNames => {
  if (!valueNames) {
    const errorsFile: GeneratedErrorsFile = errors
    const built = buildValueNames({
      enums: enumsFile,
      board: board satisfies GeneratedBoardFile,
      blocks: vmBlocks satisfies GeneratedVmBlocksIndex,
      errors: runitErrorCatalog(),
      commands: runitCommandCatalog(),
    })
    const unsupported = Object.keys(errorsFile.id_kinds).filter((kind) => !built.idKinds.has(kind))
    if (unsupported.length) throw new DescriptorError(`errors.generated.json uses @id kinds the app can't name yet: ${unsupported.join(', ')} (domain/descriptors/valueNames.ts).`)
    for (const tag of errorsFile.tags) {
      for (const field of tag.payload.fields) {
        if (field.enum_ref !== undefined && !enumsFile.enums[field.enum_ref]) throw new DescriptorError(`${tag.name}.${field.name} names enum ${field.enum_ref}, which enums.json lacks.`)
      }
    }
    valueNames = built
  }
  return valueNames
}
