import type { ServiceCatalog } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import { deviceCall } from '../protocol/services'
import type { RunitLink } from './link'
import type { ProgramFrame } from './send'

/*
 * Leaving the board's safe state (RUNIT.MD "Safe state": the VM stopped, then every device suspended), which STOP, the dead-man
 * and a lost link enter and nothing on the board leaves. The way out is the way in, reversed: every device resumed (the RESUME
 * sweep, sent to the device ID that addresses them all: SYS_DEVICE.MD "Sweeps over the router"; the board resumes them oldest
 * first), then the program started again with an EXEC the caller chooses (the engine's frame: `engine.exec('normal')`). The
 * program stays loaded through the safe state; a board that restarted has lost it (DeadMan's `restarted`): run the stored
 * project (`runProject`) or load the program again (`loadProgram`) instead.
 *
 * It is a choice, never automatic: motors move again when the devices resume.
 */

/** The commands of the way out, in their descriptors (`contract_device.json`, `contract_vm.json`). */
export const SAFE_STATE_COMMANDS = {
  devices: 'DEVICE',
  resume: 'RESUME',
  vm: 'VM',
  exec: 'EXEC',
} as const

export interface LeaveSafeStateOptions {
  /** The EXEC that starts the program again (`engine.exec('normal')`); omitted: the VM stays stopped. */
  readonly start?: ProgramFrame
  readonly timeoutMs?: number
  readonly catalog?: ServiceCatalog
}

/**
 * Resumes every device, then sends `start`. Throws the link's `CommandError` as it is: a failed RESUME (the first device that
 * failed: the others were still tried) leaves the VM stopped; a refused EXEC leaves the devices resumed and the program stopped.
 * A critical device fault latched by the VM refuses every start (`ERR_VM_EXEC_FAULT_LATCHED`, VM_CORE.MD) until the caller, who
 * shows the fault, sends `engine.exec('ackFault')`.
 */
export const leaveSafeState = async (link: RunitLink, options: LeaveSafeStateOptions = {}): Promise<void> => {
  const catalog = options.catalog ?? bundledServiceCatalog()
  if (catalog.allDevices === undefined) throw new Error('The device ID that addresses every device (SYS_DEVICE_ID_NONE) is not published.')
  const vm = catalog.service(SAFE_STATE_COMMANDS.vm)
  const exec = catalog.command(vm, SAFE_STATE_COMMANDS.exec).op
  if (options.start && options.start.op !== exec) throw new Error(`The start is ${SAFE_STATE_COMMANDS.vm} ${SAFE_STATE_COMMANDS.exec} (op ${exec}), not op ${options.start.op}.`)

  const resume = deviceCall(link.defs, catalog, catalog.allDevices, SAFE_STATE_COMMANDS.devices, SAFE_STATE_COMMANDS.resume)
  await link.request(resume.request, { timeoutMs: options.timeoutMs, label: `${resume.label} (every device)` })
  if (options.start) await link.request({ keys: [vm.key!, exec], payload: options.start.payload }, { timeoutMs: options.timeoutMs, label: `${vm.id} ${SAFE_STATE_COMMANDS.exec}` })
}
