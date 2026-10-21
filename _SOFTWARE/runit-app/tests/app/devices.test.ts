import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledLinkDefinitions, bundledServiceCatalog, createDeviceCall, deviceCall, deviceOp, onboardActionIds, readDeviceFrame, requestFrame, withRanges } from '../../src/backend'
import { bundledEnums } from '../../src/backend'
import { emptyProject } from '../../src/app/project/projectFile'
import type { ProjectFile } from '../../src/app/project/projectFile'
import {
  actionRecordOf, admittedSeverities, deviceChannels, devicePins, offersMember, pinReady, pinStateOf, baseConfig, changesPin, decodeOps, OPS_SIDE_BIT, GPIO_MODE_MEMBER, renumberDevice, isReserved, pinStartOf, withPinStart, commandGroups, DEVICE_EVENTS, describeStep, pinChoiceLabel, pinHint, pinNamer, pinNumber, showValue, DEVICE_COMMANDS, deviceList, deviceProblems, deviceRecordOf, deviceSections, fromBoard, lifecycleOf, nextActionId, nextDeviceId, stepFrame, usedBy,
} from '../../src/app/devices/devices'

const defs = bundledLinkDefinitions()
const catalog = bundledServiceCatalog()
const board = bundledBoard()
const pwm = board.devices.find((d) => d.descriptor === 'device_pca9685')!
const expander = board.devices.find((d) => d.descriptor === 'device_tca6424a')!

/** A servo on a channel of the board's PWM expander, with a start command; a start command of the expander; an action. */
const project: ProjectFile = {
  ...emptyProject('Arm'),
  devices: [{
    id: 20, type: 'servo', name: 'Left servo',
    config: { pwm_pin: { device: pwm.id, pin: 2 }, servo_type: 0, byte_mode: 0, invert: 1, angle_min_deg: 0, angle_max_deg: 180, default_pos: 90, min_us: 500, max_us: 2400, deadband_us: 0 },
    defaults: [{ kind: 'device', device: 20, contract: 'servo', command: 'target', args: { value_x10: 450 } }],
  }],
  boardDefaults: [{ device: expander.id, steps: [{ kind: 'device', device: expander.id, contract: 'IO', command: 'LEVEL', args: { pin: 22, level: 1 } }] }],
  actions: [{ id: 10, name: 'Wave', steps: [{ kind: 'device', device: 20, contract: 'servo', command: 'home' }, { kind: 'frame', bytes: [defs.keys.sys!, defs.dir.write, 0x02] }] }],
}

describe('the devices of the view', () => {
  it('the device contract has the commands the view sends by itself', () => {
    const lifecycle = lifecycleOf(catalog)
    for (const id of Object.values(DEVICE_COMMANDS)) expect(lifecycle.commands.has(id), id).toBe(true)
    for (const id of Object.values(DEVICE_EVENTS)) expect(lifecycle.events.some((e) => e.id === id), id).toBe(true)
  })

  it('lists the board\'s devices from board.json, then the project\'s, with the pins they use and who uses a pin', () => {
    const devices = deviceList(project, catalog, board)
    expect(devices.filter((d) => d.system).map((d) => d.id)).toEqual(board.devices.map((d) => d.id))
    expect(devices.every((d) => d.type !== undefined)).toBe(true)
    const servo = devices.find((d) => d.id === 20)!
    expect(servo.uses).toEqual([expect.objectContaining({ field: 'pwm_pin', pin: { device: pwm.id, pin: 2 } })])
    expect(usedBy(devices, pwm.id).map((u) => u.by.name)).toContain('Left servo')
    // A board device's pins come from board.json: the PWM expander's output enable is a pin of the GPIO expander.
    expect(devices.find((d) => d.id === pwm.id)!.uses[0]).toMatchObject({ field: 'oe_pin', pin: { device: expander.id } })
  })

  it('names a pin #n, the descriptor\'s name only a hint', () => {
    expect(pinNumber(22)).toBe('#22')
    expect(pinHint({ type: catalog.devices.get('tca6424a') }, 22)).toBe('P26')
    expect(pinHint({ type: catalog.devices.get('esp32s3_gpio') }, 21)).toBeUndefined()
  })

  it("a pin the user named is called by that name in a command and in a pin reference", () => {
    const named: ProjectFile = { ...project, pinAliases: [{ device: expander.id, pin: 22, name: 'LED blue' }, { device: pwm.id, pin: 3, name: 'Arm' }] }
    const devices = deviceList(named, catalog, board)
    const pinName = pinNamer(devices)
    expect(pinName(expander.id, 22)).toBe('#22 LED blue')
    expect(pinName(expander.id, 21)).toBe('#21')
    const nameOf = (id: number) => devices.find((d) => d.id === id)?.name
    expect(describeStep(catalog, named.boardDefaults[0]!.steps[0]!, nameOf, pinName).args).toBe('Pin #22 LED blue · Level High')
    const servo = catalog.devices.get('servo')!.config!.fields.find((f) => f.id === 'pwm_pin')!
    expect(showValue(servo, { device: pwm.id, pin: 3 }, nameOf, pinName)).toBe(`${pwm.name} · #3 Arm`)
  })

  it('a pin another device uses has no alias: its use names it', () => {
    // CH2 of the PWM expander is the servo's PWM pin in `project`.
    const named: ProjectFile = { ...project, pinAliases: [{ device: pwm.id, pin: 2, name: 'Arm' }] }
    expect(pinNamer(deviceList(named, catalog, board))(pwm.id, 2)).toBe('#2')
    expect(pinChoiceLabel(deviceList(named, catalog, board), pwm.id, 2)).toBe('#2 Left servo · PWM pin')
  })

  it('a write changes the pin it is given, a read does not; the IO and PINS commands have the sides that say so', () => {
    const io = catalog.contracts.get('IO')!
    const pins = catalog.contracts.get('PINS')!
    expect(changesPin('write')).toBe(true)
    expect(changesPin('read')).toBe(false)
    expect(['LEVEL', 'TOGGLE', 'PWM_DUTY', 'MODE', 'RESET', 'INTR', 'VOLTAGE'].every((id) => io.commands.get(id)!.write !== undefined)).toBe(true)
    expect(['LOCK', 'UNLOCK', 'LOCKED_LEVEL'].every((id) => pins.commands.get(id)!.write !== undefined)).toBe(true)
    // the mirrors: the read takes the pin, the write the pin and the value
    expect(['LEVEL', 'VOLTAGE'].every((id) => io.commands.get(id)!.read?.args?.fields.map((f) => f.id).join() === 'pin')).toBe(true)
    expect(['AVAILABLE', 'LOCKED'].every((id) => pins.commands.get(id)!.write === undefined)).toBe(true)
  })

  it('offers a device its contracts and its own ops, not the device contract (the panel has it on top)', () => {
    const servo = catalog.devices.get('servo')!
    const groups = commandGroups(catalog, servo)
    expect(groups.map((g) => g.entry.id)).toEqual(['servo'])
  })

  it('the lifecycle commands the panel shows elsewhere are not listed; with OPS only the sides the device answers are offered', () => {
    const lifecycle = lifecycleOf(catalog)
    const pca = catalog.devices.get('pca9685')!
    const ids = (groups: ReturnType<typeof commandGroups>) => groups.flatMap((g) => g.commands.map((c) => `${g.entry.id}.${c.command.id}.${c.side}`))
    expect(ids(commandGroups(catalog, pca)).filter((id) => id.startsWith(`${lifecycle.id}.`))).toEqual([])
    // a mirror is offered as its read and its write
    expect(ids(commandGroups(catalog, pca))).toEqual(expect.arrayContaining(['IO.LEVEL.read', 'IO.LEVEL.write']))
    const io = catalog.contracts.get('IO')!
    const ops = new Map([
      [deviceOp(catalog, lifecycle, lifecycle.commands.get(DEVICE_COMMANDS.info)!), OPS_SIDE_BIT.read],
      [deviceOp(catalog, io, io.commands.get('PWM_DUTY')!), OPS_SIDE_BIT.write],
      [deviceOp(catalog, io, io.commands.get('LEVEL')!), OPS_SIDE_BIT.write], // the chip drives the level but cannot read it back
    ])
    expect(ids(commandGroups(catalog, pca, { ops }))).toEqual(['IO.LEVEL.write', 'IO.PWM_DUTY.write'])
    expect(decodeOps([{ op: 0x0100, sides: 3 }])).toEqual(new Map([[0x0100, 3]]))
  })

  it('a device frame reads back as the command it was and its side, a CREATE as the type and its config', () => {
    const set = requestFrame(deviceCall(defs, catalog, 3, 'IO', 'LEVEL', { pin: 22, level: 'High' }, 'write').request)
    expect(readDeviceFrame(defs, catalog, set)).toMatchObject({ device: 3, entry: { id: 'IO' }, command: { id: 'LEVEL' }, side: 'write', args: { pin: 22, level: 1 } })
    const readFrame = requestFrame(deviceCall(defs, catalog, 3, 'IO', 'LEVEL', { pin: 22 }, 'read').request)
    expect(readDeviceFrame(defs, catalog, readFrame)).toMatchObject({ side: 'read', args: { pin: 22 } })
    const own = requestFrame(deviceCall(defs, catalog, 20, 'servo', 'target', { value_x10: -300 }).request)
    expect(readDeviceFrame(defs, catalog, own)).toBeUndefined() // whose own op it is needs the type
    expect(readDeviceFrame(defs, catalog, own, () => catalog.devices.get('servo'))).toMatchObject({ command: { id: 'target' }, args: { value_x10: -300 } })
    const create = requestFrame(createDeviceCall(defs, catalog, 20, 'servo', project.devices[0]!.config).request)
    const read = readDeviceFrame(defs, catalog, create)
    expect(read?.create?.type.id).toBe('servo')
    expect(read?.create?.config).toMatchObject({ pwm_pin: { device: pwm.id, pin: 2 }, invert: 1, max_us: 2400 })
  })

  it('the sections are the board devices with start commands, then the project devices: CREATE first', () => {
    const sections = deviceSections(defs, catalog, project, board)
    expect(sections.map((s) => s.id)).toEqual([deviceRecordOf(expander.id), deviceRecordOf(20)])
    expect(sections[1]!.frames[0]).toEqual(requestFrame(createDeviceCall(defs, catalog, 20, 'servo', project.devices[0]!.config).request))
    expect(sections[1]!.frames).toHaveLength(2)
  })

  it('what is written reads back as the same devices, start commands and actions', () => {
    const sections = deviceSections(defs, catalog, project, board)
    const actions = project.actions.map((a) => ({ id: actionRecordOf(a.id), frames: a.steps.map((s) => stepFrame(defs, catalog, s)) }))
    const read = fromBoard(defs, catalog, board, { ...emptyProject('Arm'), devices: [{ id: 20, type: 'servo', name: 'Left servo' }] }, { devices: sections, actions })
    expect(read.notes).toEqual([])
    expect(read.project.devices).toEqual([{ ...project.devices[0], config: expect.objectContaining(project.devices[0]!.config) }])
    expect(read.project.boardDefaults).toEqual(project.boardDefaults)
    expect(read.project.actions).toEqual([{ id: 10, name: 'Action 10', steps: project.actions[0]!.steps }])
  })

  it('the firmware\'s onboard actions are never the project\'s', () => {
    const onboard = onboardActionIds()
    expect(onboard.size).toBeGreaterThan(0)
    let p = emptyProject()
    for (let i = 0; i < 8; i++) p = { ...p, actions: [...p.actions, { id: nextActionId(p), name: '', steps: [] }] }
    expect(p.actions.some((a) => onboard.has(a.id))).toBe(false)
    const read = fromBoard(defs, catalog, board, emptyProject(), { devices: [], actions: [...onboard].map((id) => ({ id: actionRecordOf(id), frames: [] })) })
    expect(read.project.actions).toEqual([])
  })

  it('a new device takes an ID above the board\'s; a device is created after the devices whose pins it uses, listed or not', () => {
    expect(nextDeviceId(emptyProject(), board)).toBe(Math.max(...board.devices.map((d) => d.id)) + 1)
    expect(deviceProblems(project, catalog, board)).toEqual([])
    const listed: ProjectFile = { ...project, devices: [{ id: 30, type: 'servo', name: 'B', config: { pwm_pin: { device: 31, pin: 0 } } }, { id: 31, type: 'pca9685', name: 'Second PWM' }] }
    expect(deviceProblems(listed, catalog, board)).toEqual([])
    expect(deviceList(listed, catalog, board).slice(-2).map((d) => d.id)).toEqual([31, 30])
  })

  it('a new project device is on the user I2C bus, not the internal one of the board', () => {
    const user = board.i2cBuses.find((b) => b.name === 'user')!
    expect(baseConfig(catalog.devices.get('ap33772s'), board)).toEqual({ i2c_bus: user.bus })
    expect(baseConfig(catalog.devices.get('servo'), board)).toEqual({}) // no bus in its config
  })

  it('an error policy answers the severities from its importance up: LOW the critical ones, CRITICAL all of them', () => {
    const severities = [1, 2, 3, 4].map((value) => ({ value }))
    const values = (importance: number) => admittedSeverities(importance, severities).map((s) => s.value)
    expect(values(0)).toEqual([])
    expect(values(1)).toEqual([4])
    expect(values(2)).toEqual([3, 4])
    expect(values(3)).toEqual([2, 3, 4])
    expect(values(4)).toEqual([1, 2, 3, 4])
  })

  it("the policy offers the board's own responses on the failing device and writes them as the u32 the firmware reads", () => {
    const members = bundledEnums().device_response?.members ?? []
    expect(members.map((m) => [m.name, m.value])).toEqual([['SYS_DEVICE_RESPONSE_SUSPEND', 0xff000001], ['SYS_DEVICE_RESPONSE_RESET', 0xff000002]])
    const call = deviceCall(defs, catalog, 20, lifecycleOf(catalog).id, 'ERROR_POLICY', { importance: 2, actions: [0, 0, members[0]!.value, members[1]!.value] }, 'write')
    // [... importance u8][4 x u32 LE]: the responses are the last two slots (HIGH, CRITICAL)
    expect(Array.from(requestFrame(call.request).slice(-8))).toEqual([0x01, 0x00, 0x00, 0xff, 0x02, 0x00, 0x00, 0xff])
  })

  it('the pins the board keeps (I2C, strapping, the expander lines) are reserved; the others are not', () => {
    const esp = board.devices.find((d) => d.descriptor === 'device_esp32s3_gpio')!.id
    for (const pin of [15, 16, 40, 41]) expect(isReserved(board, esp, pin), `pin ${pin}`).toBe(true)
    expect(isReserved(board, esp, 3)).toBe(false)
  })

  const interrupt = (mode: number, debounce: boolean) => ({ mode, debounce, upMv: 0, downMv: 0, hysteresisMv: 0, count: 0 })
  it("a user pin's start settings round trip through the start commands; an interrupt of mode 0 and an unset mode have no step", () => {
    const esp = board.devices.find((d) => d.descriptor === 'device_esp32s3_gpio')!.id
    const modes = bundledEnums().sys_io_mode_e!.members
    const intr = bundledEnums().sys_io_intr_mode_e!.members
    const rising = intr.find((m) => m.name === 'SYS_IO_INTR_MODE_RISING_EDGE')!.value
    const pullup = modes.find((m) => m.name === 'SYS_IO_MODE_INPUT_PULLUP')!.value
    let steps = withPinStart([], esp, 5, { mode: pullup, interrupt: interrupt(rising, true) })
    expect(pinStartOf(steps, esp, 5)).toEqual({ mode: pullup, interrupt: interrupt(rising, true) })
    expect(pinStartOf(steps, esp, 6)).toEqual({})
    steps = withPinStart(steps, esp, 5, { mode: pullup, interrupt: interrupt(0, false) })
    expect(steps.map((s) => s.kind === 'device' ? s.command : s.kind)).toEqual(['MODE'])
    steps = withPinStart(steps, esp, 5, {})
    expect(steps).toEqual([])
    // Each step encodes: the firmware takes every argument of INTR.
    const full = withPinStart([], esp, 5, { mode: pullup, interrupt: interrupt(rising, true) })
    for (const step of full) expect(() => stepFrame(defs, catalog, step)).not.toThrow()
  })

  it('the GPIO modes a pin can take are the members of sys_io_mode_e', () => {
    const names = new Set(bundledEnums().sys_io_mode_e!.members.map((m) => m.name))
    for (const name of Object.values(GPIO_MODE_MEMBER)) expect(names.has(name), name).toBe(true)
  })

  it('a start command of an edge interrupt shows its mode, not the window fields an ADC window takes', () => {
    const esp = board.devices.find((d) => d.descriptor === 'device_esp32s3_gpio')!.id
    const rising = bundledEnums().sys_io_intr_mode_e!.members.find((m) => m.name === 'SYS_IO_INTR_MODE_RISING_EDGE')!.value
    const [step] = withPinStart([], esp, 5, { interrupt: interrupt(rising, true) })
    expect(describeStep(catalog, step!, () => undefined, pinNamer([])).args).not.toMatch(/threshold|Hysteresis/)
  })

  it('renumbering a device moves its ID and every reference to it', () => {
    const moved = renumberDevice({ ...project, devices: [...project.devices, { id: 30, type: 'pca9685', name: 'Other', config: {}, defaults: [] }], boardDefaults: [] }, 20, 40)
    expect(moved.devices.map((d) => d.id)).toEqual([40, 30])
    const [first] = moved.devices
    expect(first?.defaults?.[0]).toMatchObject({ device: 40 })
    expect(moved.actions[0]?.steps[0]).toMatchObject({ device: 40 })
    expect(renumberDevice({ ...project, devices: [{ id: 31, type: 'servo', name: 'B', config: { pwm_pin: { device: 20, pin: 1 } }, defaults: [] }] }, 20, 40).devices[0]!.config).toMatchObject({ pwm_pin: { device: 40, pin: 1 } })
  })

  it('a bitmask field names its bits: the form offers only those', () => {
    const field = catalog.devices.get('ina3221')!.config!.fields.find((f) => f.id === 'inverted_mask')!
    expect(field.type).toBe('bitmask')
    expect(field.bits?.map((b) => b.bit)).toEqual([0, 1, 2])
  })
})

describe('the pins a command is offered', () => {
  const gpio = deviceList(emptyProject('x'), catalog, board).find((d) => d.type?.id === 'esp32s3_gpio')!
  const withStart = (steps: ProjectFile['devices'][number]['defaults']): typeof gpio => ({ ...gpio, defaults: steps ?? [] })
  const mode = (name: string) => bundledEnums().sys_io_mode_e!.members.find((m) => m.name === name)!.value
  const io = (command: string, args: Record<string, number>) => ({ kind: 'device' as const, device: gpio.id, contract: 'IO', command, args })

  it('every IO command that takes a pin says in which state, from the descriptor', () => {
    const io = catalog.contracts.get('IO')!
    const wants = (id: string, side: 'read' | 'write' = 'write') => io.commands.get(id)![side]!.args!.fields.find((f) => f.id === 'pin')!.pinState
    expect(wants('LEVEL')).toEqual(['output'])
    expect(wants('LEVEL', 'read')).toEqual(['input', 'output'])
    expect(wants('PWM_DUTY')).toEqual(['pwm'])
    expect(wants('MODE')).toEqual(['unconfigured'])
    expect(wants('RESET')).toEqual(['configured'])
  })

  it('a pin has no mode until something gives it one: the start commands, then what this session did live', () => {
    expect(pinStateOf(gpio, board, 4, true)).toEqual(['unconfigured'])
    const started = withStart([io('MODE', { pin: 4, mode: mode('SYS_IO_MODE_PWM') })])
    expect(pinStateOf(started, board, 4, true)).toEqual(['pwm'])
    expect(pinStateOf(started, board, 5, true)).toEqual(['unconfigured'])
    // a RESET after it clears it; a live change goes before the project's
    expect(pinStateOf(withStart([...started.defaults, io('RESET', { pin: 4 })]), board, 4, true)).toEqual(['unconfigured'])
    expect(pinStateOf(started, board, 4, true, new Map([[`${gpio.id}/4`, 'output']]))).toEqual(['output'])
    expect(pinStateOf(started, board, 4, true, new Map([[`${gpio.id}/4`, null]]))).toEqual(['unconfigured'])
  })

  it('a pin is ready for a command that wants one of its states; configured is any mode', () => {
    expect(pinReady(['unconfigured'], ['output'])).toBe(false)
    expect(pinReady(['output'], ['output'])).toBe(true)
    expect(pinReady(['input'], ['input', 'output'])).toBe(true)
    expect(pinReady(['pwm'], ['configured'])).toBe(true)
    expect(pinReady(['unconfigured'], ['configured'])).toBe(false)
    expect(pinReady(['pwm'], ['unconfigured'])).toBe(false)
    expect(pinReady(['unconfigured'], undefined)).toBe(true)
  })

  it('a device whose pins have a mode after reset (default_mode) keeps the MODE write open and its pins configured', () => {
    const tca = deviceList(emptyProject('x'), catalog, board).find((d) => d.type?.id === 'tca6424a')!
    expect(pinStateOf(tca, board, 3, true)).toEqual(['input', 'unconfigured'])
    expect(pinReady(pinStateOf(tca, board, 3, true), ['configured'])).toBe(true)
    expect(pinReady(pinStateOf(tca, board, 3, true), ['unconfigured'])).toBe(true)
    expect(pinReady(pinStateOf(tca, board, 3, true), ['output'])).toBe(false)
    expect(pinStateOf(tca, board, 3, true, new Map([[`${tca.id}/3`, 'output']]))).toEqual(['output', 'unconfigured'])
  })

  it('a device without the MODE write has its pins fixed: the states its descriptor offers', () => {
    const dac = deviceList(emptyProject('x'), catalog, board).find((d) => d.type?.id === 'dac53202')
    if (dac) expect(pinStateOf(dac, board, 0, false)).toEqual(['dac'])
  })

  it("a mode field offers only what the device's pins can be, an interrupt field what its pins can raise", () => {
    expect(offersMember(gpio, 'sys_io_mode_e', mode('SYS_IO_MODE_PWM'))).toBe(true)
    expect(offersMember(gpio, 'sys_io_mode_e', mode('SYS_IO_MODE_DAC'))).toBe(false)
    expect(offersMember(gpio, 'sys_io_intr_mode_e', 1)).toBe(true)
    expect(offersMember(gpio, 'sys_io_intr_mode_e', 4)).toBe(false)
    expect(offersMember(gpio, 'other', 1)).toBe(true)
  })
})


describe('live settings and channels that follow the config', () => {
  it('the DRV8962: its settings command writes topology, limit and braking live; its channels follow the topology', async () => {
    const { liveSettingsOf, liveSettingsArgs, startSettings, devicePins, deviceList } = await import('../../src/app/devices/devices')
    const catalog = bundledServiceCatalog()
    const drv = catalog.devices.get('drv8962')
    const live = liveSettingsOf(drv)
    expect(live?.id).toBe('settings')
    expect(liveSettingsOf(catalog.devices.get('tca6424a'))).toBeUndefined()
    expect(liveSettingsArgs(live!, { topology: 1, current_limit_mA: 1500, turn_off_at_ocp: 0, pwm_freq_Hz: 5 })).toEqual({ topology: 1, current_limit_mA: 1500, turn_off_at_ocp: 0 })
    const board = deviceList(emptyProject(), catalog, bundledBoard()).find((d) => d.id === 5)!
    expect(devicePins(board)).toMatchObject({ count: 2, labels: ['Bridge A (OUT1+OUT2)', 'Bridge B (OUT3+OUT4)'] })
    expect(devicePins({ ...board, config: { ...board.config, topology: 1 } })).toMatchObject({ count: 4, labels: ['OUT1', 'OUT2', 'OUT3', 'OUT4'] })
    const withStep = { ...board, defaults: [{ kind: 'device' as const, device: 5, contract: 'drv8962', command: 'settings', args: { topology: 1, current_limit_mA: 900, turn_off_at_ocp: 1 } }] }
    expect(startSettings(withStep, live!)).toEqual({ topology: 1, current_limit_mA: 900, turn_off_at_ocp: 1 })
    // A board device's live settings in its start commands are its config: four half bridges give four channels.
    const project = { ...emptyProject(), boardDefaults: [{ device: 5, steps: withStep.defaults }] }
    const halved = deviceList(project, catalog, bundledBoard()).find((d) => d.id === 5)!
    expect(halved.config.topology).toBe(1)
    expect(devicePins(halved)?.count).toBe(4)
  })
})

describe('a MODE write with nothing to choose', () => {
  it('is not offered on a device whose pins take one mode from reset (the ADS7128), and is on the ESP GPIO', async () => {
    const { commandGroups } = await import('../../src/app/devices/devices')
    const catalog = bundledServiceCatalog()
    const ioCommands = (type: string) => commandGroups(catalog, catalog.devices.get(type)).find((g) => g.entry.id === 'IO')?.commands.map((c) => c.command.id) ?? []
    expect(ioCommands('ads7128')).not.toContain('MODE')
    expect(ioCommands('ads7128')).toContain('VOLTAGE')
    expect(ioCommands('esp32s3_gpio')).toContain('MODE')
  })
})

describe('IO commands a device\'s pins can never take', () => {
  it('are not offered: the ADS7128 has Reset, Interrupt and the voltage read only; the ESP GPIO keeps levels and PWM', async () => {
    const { commandGroups, sideLabel } = await import('../../src/app/devices/devices')
    const catalog = bundledServiceCatalog()
    const io = (type: string) => commandGroups(catalog, catalog.devices.get(type)).find((g) => g.entry.id === 'IO')?.commands.map(sideLabel) ?? []
    expect(io('ads7128')).toEqual(['Reset pin', 'Interrupt', 'Voltage: read'])
    expect(io('esp32s3_gpio')).toEqual(expect.arrayContaining(['Level: write', 'PWM duty', 'Mode']))
    expect(io('dac53202')).toEqual(['Reset pin', 'Voltage: read', 'Voltage: write'])
  })
})

describe('the ranges a device gives its commands', () => {
  it('a regulator rail of the runIT board takes 4 to 20 V in steps of 100 mV, and a layout carries it', () => {
    const rail = deviceList(emptyProject('Rails'), catalog, board).find((d) => d.name === 'Rail A')!
    expect(rail.ranges?.['VOLTAGE.voltage_mV']).toEqual({ min: 4000, max: 20000, step: 100 })
    const voltage = catalog.contracts.get('POWER_VREG')!.commands.get('VOLTAGE')!.write!.args!
    expect(voltage.fields[0]).toMatchObject({ control: 'slider' })
    expect(voltage.fields[0]!.max).toBeUndefined()
    expect(withRanges(voltage, 'VOLTAGE', rail.ranges)!.fields[0]).toMatchObject({ min: 4000, max: 20000, step: 100, control: 'slider' })
  })
  it('a command without a range keeps its layout', () => {
    const voltage = catalog.contracts.get('POWER_VREG')!.commands.get('VOLTAGE')!.write!.args!
    expect(withRanges(voltage, 'CURRENT', { 'VOLTAGE.voltage_mV': { min: 1, max: 2 } })!.fields[0]!.max).toBeUndefined()
  })
})

describe('the channels of a device', () => {
  const monitor = deviceList(emptyProject('Monitor'), catalog, board).find((d) => d.type?.id === 'ina3221')!
  it("a power monitor's channel field is a channel, and its device names them", () => {
    const field = catalog.contracts.get('POWER_MONITOR')!.commands.get('CURRENT')!.read!.args!.fields[0]!
    expect(field.type).toBe('channel')
    expect(deviceChannels(monitor)).toEqual({ count: 3, labels: ['Channel 1', 'Channel 2', 'Channel 3'] })
  })
  it('a device whose channels are its pins gives them', () => {
    const driver = deviceList(emptyProject('Driver'), catalog, board).find((d) => d.type?.id === 'drv8962')!
    expect(deviceChannels(driver)?.count).toBe(devicePins(driver)!.count)
  })
})
