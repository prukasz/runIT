import { describe, expect, it } from 'vitest'
import { runitCommandCatalog, runitDeviceCatalog, runitStreamCatalog, runitVmCatalog } from '../../../src/domain/descriptors'
import { boardDeviceRef, createProject } from '../../../src/domain/project'
import type { ActionStep, ProjectAction, ProjectDevice } from '../../../src/domain/project'
import { buildStoredCode, crc32, encodeFrameList } from '../../../src/domain/storedCode'
import { boardDefaultSettings, runitSettingsIds } from '../../../src/domain/upload'
import { actionRecordSteps, actionRemoveStep, actionRunStep, buildAction, contractFrame, defaultInstall, deviceInstallSteps, deviceSetupSteps, encodeErrorActions, findContract, installFrame, pinKey, pinsOf, pinUsers, resolveDevice, SET_ERROR_HANDLING } from '../../../src/domain/devices'

/*
 * Frames the app's UI builds, saved as a fixture (fixtures/ui-samples.json)
 * for the board test (.claude/skills/runit-esp/scripts/ui_samples_test.py),
 * which sends them to a board and saves what it answered next to them.
 * Regenerate after a descriptor change: npx vitest run uiSamples -u
 *
 * `expect`: "ok", an error tag the board must answer, or "ok|<tag>" when the
 * answer depends on what is wired (a device on the user I2C bus). `fields`:
 * response values the board must return.
 */

const catalog = runitDeviceCatalog()
const commands = runitCommandCatalog()
const hex = (frame: Uint8Array) => [...frame].map((byte) => byte.toString(16).padStart(2, '0')).join('')

interface Sample {
  readonly label: string
  readonly frame: string
  readonly expect: string
  readonly fields?: Readonly<Record<string, number>>
}

const esp = resolveDevice(catalog, [], boardDeviceRef(0))!
const tca = resolveDevice(catalog, [], boardDeviceRef(1))!
const dac = resolveDevice(catalog, [], boardDeviceRef(4))!
const mode = (symbol: string) => catalog.ioModes.find((entry) => entry.symbol === symbol)!.value
const staticAction = (symbol: string) => catalog.staticActions.find((entry) => entry.symbol === symbol)!.value

/** ESP32-S3 strapping pins (0, 3, 45, 46) and USB D-/D+ (19, 20): the board doesn't list them, a test leaves them alone. */
const UNTOUCHABLE = [0, 3, 19, 20, 45, 46]
/** Two ESP pins nothing on the board is wired to (not even the devices this board doesn't install). */
const wired = new Set([...pinUsers(catalog, []).keys(), ...catalog.board.flatMap((device) => device.pins.map((link) => pinKey(link.deviceId, link.pin)))])
const [pinA, pinB] = pinsOf(esp.type).map((choice) => choice.value).filter((pin) => !wired.has(pinKey(0, pin)) && !UNTOUCHABLE.includes(pin))

const call = (label: string, device: typeof esp, contract: string, values: ActionStep['values'], expected = 'ok', fields?: Record<string, number>): Sample => ({
  label,
  frame: hex(contractFrame(findContract(catalog, device, contract)!, device.deviceId, values)),
  expect: expected,
  ...(fields ? { fields } : {}),
})
const lifecycle = (name: string, device: typeof esp, expected = 'ok'): Sample => {
  const contract = catalog.lifecycle.find((entry) => entry.command.name === `sys_device_${name}`)!
  return { label: `${device.name}: ${name}`, frame: hex(contractFrame(contract, device.deviceId, {})), expect: expected }
}

// Default settings (DefaultSettingsCard): the level before the mode in the project, pin modes still go first.
const setup: ActionStep[] = [
  { id: 's1', device: esp.ref, contract: 'packet_sys_io_set_level_t', values: { pin: pinA!, level: 1 } },
  { id: 's2', device: esp.ref, contract: 'packet_sys_io_set_mode_t', values: { pin: pinA!, mode: mode('SYS_IO_MODE_OUTPUT_PUSH_PULL') } },
  { id: 's3', device: esp.ref, contract: 'packet_sys_io_set_mode_t', values: { pin: pinB!, mode: mode('SYS_IO_MODE_INPUT_PULLUP') } },
]

// An action (ActionComposer): pin A low, then high; recorded under a high ID (dynamic IDs go to 255).
const action: ProjectAction = {
  id: 'a1',
  actionId: 200,
  name: 'pin A pulse',
  steps: [
    { id: 'p1', device: esp.ref, contract: 'packet_sys_io_set_level_t', values: { pin: pinA!, level: 0 } },
    { id: 'p2', device: esp.ref, contract: 'packet_sys_io_set_level_t', values: { pin: pinA!, level: 1 } },
  ],
}

// User devices listed out of order: the PCA's OE pin is on the user TCA, which has the lower ID.
const userTca: ProjectDevice = { id: 'tca', deviceId: 20, type: 'device_tca6424a', name: 'user io', tags: [], install: { ...defaultInstall(catalog.type('device_tca6424a')!, catalog.i2cBuses.user), i2c_addr: 0x22 } }
const userPca: ProjectDevice = { id: 'pca', deviceId: 21, type: 'device_pca9685', name: 'user pwm', tags: [], install: { ...defaultInstall(catalog.type('device_pca9685')!, catalog.i2cBuses.user), i2c_addr: 0x40, oe_pin_device_id: 20, oe_pin_pin: 0, oe_pin_mode: mode('SYS_IO_MODE_OUTPUT_PUSH_PULL') } }

const samples = () => {
  const setupSteps = deviceSetupSteps(catalog, [], setup)
  const build = buildAction(catalog, [], action)
  const installs = deviceInstallSteps(catalog, [userPca, userTca])
  expect([...setupSteps.diagnostics, ...build.diagnostics, ...installs.diagnostics]).toEqual([])

  const ok = (steps: readonly { label: string; frame: Uint8Array }[]): Sample[] => steps.map((step) => ({ label: step.label, frame: hex(step.frame), expect: 'ok' }))
  const errorHandling = (importance: number, actions: number[], expected = 'ok'): Sample =>
    call(`DAC53202: error handling importance ${importance}, actions [${actions.join(', ')}]`, dac, SET_ERROR_HANDLING, { importance, actions }, expected)

  const defaults = boardDefaultSettings()
  const ctx = { vm: runitVmCatalog(), commands, layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: catalog }
  const code = buildStoredCode({ project: createProject('ui samples'), settings: defaults, boardDefaults: defaults, setup }, ctx)
  expect(code.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
  const blob = encodeFrameList(code.steps.map((step) => step.frame))

  return {
    about: 'Frames built by the app UI (app/tests/domain/devices/uiSamples.test.ts); sent to a board by .claude/skills/runit-esp/scripts/ui_samples_test.py. Frames have no seq byte.',
    pins: { a: pinA, b: pinB },
    groups: [
      {
        name: 'Default settings (pin modes first)',
        samples: [
          ...ok(setupSteps.steps),
          call('ESP GPIO: get level A', esp, 'packet_sys_io_get_level_t', { pin: pinA! }, 'ok', { level: 1 }),
          call('ESP GPIO: toggle A', esp, 'packet_sys_io_toggle_t', { pin: pinA! }),
          call('ESP GPIO: get level A', esp, 'packet_sys_io_get_level_t', { pin: pinA! }, 'ok', { level: 0 }),
          call('ESP GPIO: get level B (pull-up)', esp, 'packet_sys_io_get_level_t', { pin: pinB! }, 'ok', { level: 1 }),
        ],
      },
      {
        name: 'Device quick commands',
        samples: [lifecycle('freeze', tca), lifecycle('sync', tca), lifecycle('resume', tca)],
      },
      {
        name: 'Action: record, run, remove',
        samples: [
          ...ok(actionRecordSteps(commands, action, build)),
          call('ESP GPIO: set level A low', esp, 'packet_sys_io_set_level_t', { pin: pinA!, level: 0 }),
          ok([actionRunStep(commands, action.actionId)])[0]!,
          call('ESP GPIO: get level A (the action left it high)', esp, 'packet_sys_io_get_level_t', { pin: pinA! }, 'ok', { level: 1 }),
        ],
      },
      {
        name: 'Error handling',
        samples: [
          // Medium: suspend all (static); Critical: the recorded action 200 (dynamic).
          errorHandling(2, encodeErrorActions([{ scope: 'static', id: 0 }, { scope: 'static', id: staticAction('SYS_ACTION_STATIC_SUSPEND') }, { scope: 'static', id: 0 }, { scope: 'dynamic', id: action.actionId }])),
          errorHandling(4, encodeErrorActions([{ scope: 'dynamic', id: 255 }, { scope: 'static', id: 0 }, { scope: 'static', id: 0 }, { scope: 'static', id: 0 }])),
          errorHandling(1, encodeErrorActions([{ scope: 'static', id: 16 }, { scope: 'static', id: 0 }, { scope: 'static', id: 0 }, { scope: 'static', id: 0 }]), 'ERR_INVALID_VAL_UI32'),
          errorHandling(0, [0, 0, 0, 0, 0]),
          ok([actionRemoveStep(commands, action.actionId)])[0]!,
        ],
      },
      {
        name: 'User device installs (by ID = dependency order; the result depends on what is on the user I2C bus)',
        samples: [
          ...installs.steps.map((step) => ({ label: step.label, frame: hex(step.frame), expect: 'ok|ERR_I2C_DEV_NOT_FOUND' })),
          // Uninstall is idempotent: OK whether the install went through or not.
          lifecycle('uninstall', { ...esp, name: 'user pwm', deviceId: 21 }),
          lifecycle('uninstall', { ...esp, name: 'user io', deviceId: 20 }),
        ],
      },
      {
        name: 'Firmware guards (frames the app refuses to build, packed directly)',
        samples: [
          // checkDevices refuses this project; the board must refuse the frame too.
          { label: 'device install user pwm (20) with its OE pin on device 21', frame: hex(installFrame(catalog.type('device_pca9685')!, { ...userPca, deviceId: 20, install: { ...userPca.install, oe_pin_device_id: 21 } })), expect: 'ERR_DEV_PIN_ORDER' },
          // planVmUpload sends nothing for an empty program.
          { label: 'vm open of an empty program', frame: '044100000000000000000000', expect: 'ERR_VM_LOAD_EMPTY' },
        ],
      },
    ],
    storedCode: {
      about: 'Stored code (settings at defaults + the default settings above): store, restart, every frame replayed, pin A high after boot.',
      frames: code.steps.map((step) => ({ label: step.label, frame: hex(step.frame) })),
      blob: hex(blob),
      crc32: crc32(blob),
    },
  }
}

describe('UI samples', () => {
  it('installs by ID and matches the saved fixture', async () => {
    const built = samples()
    expect(built.groups[4]!.samples.slice(0, 2).map((sample) => sample.label)).toEqual(['device install user io (20)', 'device install user pwm (21)'])
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/ui-samples.json')
  })
})
