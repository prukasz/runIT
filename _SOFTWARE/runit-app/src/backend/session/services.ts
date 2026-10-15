import type { FieldValues } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import { createDeviceCall, decodeAnswer, deviceCall, serviceCall } from '../protocol/services'
import type { Decoded, ServiceCall } from '../protocol/services'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'

/*
 * Every command of every router, sent over the link as its descriptor describes it (`protocol/services.ts`): the arguments
 * encoded by field id, the answer decoded. The catalog is what the firmware publishes; nothing here names a service.
 *
 *   const services = new Services(link)
 *   const { value } = await services.call('SYS', 'PING')                         // { uptime_ms: 1234 }
 *   await services.device(3, 'IO', 'SET_LEVEL', { pin: 22, level: 'High' })
 *   await services.create(20, 'servo', { pwm_pin: [3, 0, 4], servo_type: 'SERVO_TYPE_ANGLE' })
 */

export interface Answer {
  /** The answer as the descriptor describes it; undefined when it describes none. */
  readonly value: Decoded | undefined
  /** The router's output as it came. */
  readonly data: Uint8Array
}

export class Services {
  constructor(
    readonly link: RunitLink,
    readonly catalog: ServiceCatalog = bundledServiceCatalog(),
  ) {}

  /** A command of a system service (`SYS`, `I2C`, `BLE`, `LOG`, ... as the catalog has them). */
  call(service: string, command: string, args?: FieldValues, options?: CommandOptions): Promise<Answer> {
    return this.send(serviceCall(this.catalog, service, command, args), options)
  }

  /** A command to a device: a contract's (`IO`, `DEVICE`, ...) or one of its type's own (`servo`). */
  device(device: number, contract: string, command: string, args?: FieldValues, options?: CommandOptions): Promise<Answer> {
    return this.send(deviceCall(this.link.defs, this.catalog, device, contract, command, args), options)
  }

  /** Creates device `device` of a type (a device descriptor's id or type id) with its config. */
  create(device: number, type: string | number, config?: FieldValues, options?: CommandOptions): Promise<Answer> {
    return this.send(createDeviceCall(this.link.defs, this.catalog, device, type, config), options)
  }

  /** Any call built with `protocol/services.ts`. */
  async send(call: ServiceCall, options?: CommandOptions): Promise<Answer> {
    const data = await this.link.request(call.request, { label: call.label, ...options })
    return { value: decodeAnswer(call, data), data }
  }
}
