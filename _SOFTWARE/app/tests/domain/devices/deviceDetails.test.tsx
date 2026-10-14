import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { RunitBleLink } from '../../../src/ble'
import { CommandFields, DeviceDetails, decodeDeviceResponse } from '../../../src/devices/DeviceDetails'
import { BOARD_DEVICES, deviceCommands, deviceType } from '../../../src/domain/devices'
import type { DevicesWorkspace } from '../../../src/devices/useDevicesWorkspace'

describe('decodeDeviceResponse', () => {
  it('returns OK for empty response', () => {
    expect(decodeDeviceResponse(new Uint8Array())).toBe('OK')
  })

  it('decodes scalar return types like voltage and current with units and alias', () => {
    const pmon = deviceType('device_ina3221')
    const commands = deviceCommands(pmon)
    const getVolt = commands.find((c) => c.symbol === 'PMON_OP_GET_VOLTAGE')
    expect(getVolt).toBeDefined()

    // 3300 mV in i32 LE = 0xe4, 0x0c, 0x00, 0x00
    const voltData = new Uint8Array([0xe4, 0x0c, 0x00, 0x00])
    expect(decodeDeviceResponse(voltData, getVolt)).toBe('Voltage: 3300 mV')

    const getCurr = commands.find((c) => c.symbol === 'PMON_OP_GET_CURRENT')
    expect(getCurr).toBeDefined()
    // -500 mA in i32 LE = 0x0c, 0xfe, 0xff, 0xff
    const currData = new Uint8Array([0x0c, 0xfe, 0xff, 0xff])
    expect(decodeDeviceResponse(currData, getCurr)).toBe('Current: -500 mA')
  })

  it('decodes enum/choice return types like level', () => {
    const gpio = deviceType('device_gpio_esp')
    const commands = deviceCommands(gpio)
    const getLevel = commands.find((c) => c.symbol === 'IO_OP_GET_LEVEL')
    expect(getLevel).toBeDefined()

    expect(decodeDeviceResponse(new Uint8Array([1]), getLevel)).toBe('Level: High')
    expect(decodeDeviceResponse(new Uint8Array([0]), getLevel)).toBe('Level: Low')
  })

  it('decodes struct return types like USB-PD limits', () => {
    const pd = deviceType('device_ap33772s')
    const commands = deviceCommands(pd)
    const getLimits = commands.find((c) => c.symbol === 'PD_OP_GET_LIMITS')
    expect(getLimits).toBeDefined()

    // 20000 mV (0x4e20) and 3000 mA (0x0bb8)
    const data = new Uint8Array([0x20, 0x4e, 0x00, 0x00, 0xb8, 0x0b, 0x00, 0x00])
    expect(decodeDeviceResponse(data, getLimits)).toBe('Voltage: 20000 mV · Current: 3000 mA')
  })
})

describe('DeviceDetails panel rendering', () => {
  const dummyLink = new RunitBleLink()

  const mockWorkspace = (selection: DevicesWorkspace['selection']): DevicesWorkspace =>
    ({
      selection,
      devices: [
        {
          id: 'dev-1',
          name: 'My Servo',
          type: 'device_servo',
          deviceId: 21,
          tags: [],
          install: {},
        },
      ],
      boardNames: {},
      pinAliases: {},
      setup: [],
      actions: [],
      onBoard: [],
      state: {} as any,
      select: () => {},
      addDevice: () => {},
      updateDevice: () => {},
      removeDevice: () => {},
      renameBoardDevice: () => {},
      boardName: (id: number) => BOARD_DEVICES.find((d) => d.id === id)?.alias ?? `Device ${id}`,
      load: () => {},
      problems: [],
      stepProblems: [],
      setPinAlias: () => {},
      setPinMode: () => {},
      setPinLevel: () => {},
      setSetup: () => {},
      addAction: () => {},
      updateAction: () => {},
      removeAction: () => {},
      addStepToAction: () => {},
      records: () => [],
      foreign: () => [],
      saved: () => {},
      synced: () => {},
    }) as unknown as DevicesWorkspace

  it('renders placeholder when no device is selected', () => {
    const html = renderToString(<DeviceDetails workspace={mockWorkspace({ kind: 'overview' })} link={dummyLink} />)
    expect(html).toContain('Select a device on the left')
  })

  it('renders board device details and commands without exposing symbol or raw type', () => {
    const html = renderToString(<DeviceDetails workspace={mockWorkspace({ kind: 'device', ref: 'board:0' })} link={dummyLink} />)
    expect(html).toContain('ESP32 GPIO')
    expect(html).toContain('#0')
    expect(html).not.toContain('DEVICE_ID_GPIO_ESP')
    expect(html).not.toContain('SYS_DEVICE_TYPE_GPIO_ESP')
    expect(html).toContain('Reset')
  })

  it('renders user device with install button and device alias', () => {
    const html = renderToString(<DeviceDetails workspace={mockWorkspace({ kind: 'device', ref: 'dev-1' })} link={dummyLink} />)
    expect(html).toContain('My Servo')
    expect(html).toContain('#21')
    expect(html).toContain('Install')
  })

  it('renders DAC command fields with ChoiceField (ListableField) for DAC channel parameter', () => {
    const dac = deviceType('device_dac53202')
    const commands = deviceCommands(dac)
    const setVolt = commands.find((c) => c.symbol === 'IO_OP_SET_VOLTAGE')!
    expect(setVolt).toBeDefined()

    const html = renderToString(<CommandFields command={setVolt} values={{}} onChange={() => {}} />)
    expect(html).toContain('DAC channel')
    expect(html).toContain('listable-field-select')
    expect(html).toContain('field-adornment-slot')
  })
})
