import type { FieldValues } from '../catalog/codec'
import type { ServiceCatalog, Side } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import { createDeviceCall, createProviderCall, createRemoteDeviceCall, decodeAnswer, deviceCall, serviceCall } from '../protocol/calls'
import type { CallArgs, Decoded, ServiceCall } from '../protocol/calls'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'

/*
 * Every command of every router, sent over the link as its descriptor describes it (`protocol/calls.ts`): the arguments encoded
 * by field id, the answer decoded. Nothing here names a service.
 *
 *   const services = new Services(link)
 *   const { value } = await services.call('SYS', 'PING')                                         // { uptime_ms: 1234 }
 *   await services.device(3, 'IO', 'LEVEL', { pin: 22, level: 'High' }, { side: 'write' })
 *   await services.create(20, 'servo', { pwm_pin: { device: 3, pin: 4 }, servo_type: 'SERVO_TYPE_ANGLE' })
 *   await services.createProvider(8, 'ble', { svc: 0xee00, tx: 0xee01, name: 'temp' })
 */

export interface Answer {
  /** The answer as the descriptor describes it; undefined when it describes none. */
  readonly value: Decoded | undefined
  /** The router's output as it came. */
  readonly data: Uint8Array
}

/** The options of a call: the link's, and the side of a command that has both. */
export interface CallOptions extends CommandOptions {
  readonly side?: Side
  /** The id a service addressed by one takes (a provider's); left out, none (PROVIDER LIST). */
  readonly id?: number
}

export class Services {
  constructor(
    readonly link: RunitLink,
    readonly catalog: ServiceCatalog = bundledServiceCatalog(),
  ) {}

  /** A command of a system service (`SYS`, `I2C`, `BLE`, `LOG`, ... as the catalog has them). */
  call(service: string, command: string, args?: CallArgs, { side, id, ...options }: CallOptions = {}): Promise<Answer> {
    return this.send(serviceCall(this.catalog, service, command, args, side, id), options)
  }

  /** A command to a device: a contract's (`IO`, `DEVICE`, ...) or one of its type's own (`servo`). */
  device(device: number, contract: string, command: string, args?: CallArgs, { side, ...options }: CallOptions = {}): Promise<Answer> {
    return this.send(deviceCall(this.catalog, device, contract, command, args, side), options)
  }

  /** Creates device `device` of a type (a device descriptor's id or type id) with its config. */
  create(device: number, type: string | number, config?: FieldValues, options?: CommandOptions): Promise<Answer> {
    return this.send(createDeviceCall(this.catalog, device, type, config), options)
  }

  /** Makes device `device` an alias of device `remoteId` of board `board` (CREATE_REMOTE). */
  createRemote(device: number, type: string | number, board: number, remoteId: number, link: 'wired' | 'wireless' = 'wireless', options?: CommandOptions): Promise<Answer> {
    return this.send(createRemoteDeviceCall(this.catalog, device, type, board, remoteId, link), options)
  }

  /** Creates provider `id` of a type (a provider descriptor's id, `ble`) with its config. */
  createProvider(id: number, type: string | number, config?: FieldValues, options?: CommandOptions): Promise<Answer> {
    return this.send(createProviderCall(this.catalog, id, type, config), options)
  }

  /** Any call built with `protocol/calls.ts`. */
  async send(call: ServiceCall, options?: CommandOptions): Promise<Answer> {
    const data = await this.link.request(call.request, { label: call.label, ...options })
    return { value: decodeAnswer(call, data), data }
  }
}
