import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../domain/descriptors'
import { boardDeviceRef } from '../domain/project'
import type { ProjectAction, ProjectDevice } from '../domain/project'
import { defaultInstall } from '../domain/devices'
import { DeviceDetails } from './DeviceDetails'
import { DevicesEditor } from './DevicesEditor'
import { DevicesPalette } from './DevicesPalette'
import { ErrorActionsField } from './ErrorHandling'
import { useDevicesWorkspace } from './useDevicesWorkspace'
import type { DeviceSelection, DevicesWorkspace } from './useDevicesWorkspace'

/* Server-renders every Board view page once, so a page that throws fails here instead of in the browser. */

const catalog = runitDeviceCatalog()
const pcaType = catalog.type('device_pca9685')!
const device: ProjectDevice = { id: 'd1', deviceId: 20, type: pcaType.id, name: 'arm', tags: ['servo'], install: { ...defaultInstall(pcaType, catalog.i2cBuses.user), i2c_addr: 0x41 }, appearance: { icon: 'robot' } }
const action: ProjectAction = { id: 'a1', actionId: 3, name: 'pose', steps: [{ id: 's1', device: 'd1', contract: 'packet_sys_io_set_pwm_duty_t', values: { pin: 2, duty: 300 } }] }

function Harness({ selection, composing, part }: { selection?: DeviceSelection; composing?: boolean; part: 'editor' | 'details' | 'palette' }) {
  const base = useDevicesWorkspace()
  const w: DevicesWorkspace = { ...base, devices: [device], actions: [action], selection, composing: composing ? action : undefined }
  if (part === 'palette') return <DevicesPalette workspace={w} />
  if (part === 'details') return <DeviceDetails workspace={w} />
  return <DevicesEditor workspace={w} />
}

describe('Board view renders', () => {
  it('overview, catalog, palette and action pages', () => {
    expect(renderToString(<Harness part="editor" />)).toContain('Board devices')
    expect(renderToString(<Harness part="editor" selection={{ kind: 'add' }} />)).toContain('PCA9685 PWM expander')
    expect(renderToString(<Harness part="palette" />)).toContain('System devices')
    expect(renderToString(<Harness part="editor" selection={{ kind: 'action', id: 'a1' }} />)).toContain('pose')
    expect(renderToString(<Harness part="editor" selection={{ kind: 'device', ref: 'd1' }} composing />)).toContain('Action composer')
  })

  it('every board device and the user device, main view and right panel', () => {
    for (const ref of [...catalog.board.map((entry) => boardDeviceRef(entry.deviceId)), 'd1']) {
      expect(renderToString(<Harness part="editor" selection={{ kind: 'device', ref }} />)).toContain('Configuration')
      expect(renderToString(<Harness part="details" selection={{ kind: 'device', ref }} />)).toContain('Device')
    }
    expect(renderToString(<Harness part="editor" selection={{ kind: 'device', ref: 'd1' }} />)).toContain('Output enable pin')
    const tca = renderToString(<Harness part="editor" selection={{ kind: 'device', ref: boardDeviceRef(1) }} />)
    expect(tca).toContain('Default settings')
    expect(tca).toContain('Status LEDs')
    expect(tca).toContain('Error handling')
    expect(tca).toContain('Suspend all devices')
    expect(renderToString(<Harness part="editor" selection={{ kind: 'device', ref: boardDeviceRef(3) }} />)).toContain('Pins it uses')
    const ads = renderToString(<Harness part="editor" selection={{ kind: 'device', ref: boardDeviceRef(2) }} />)
    expect(ads.match(/<small>Device default<\/small>/g)).toHaveLength(8)
    expect(ads).toContain('ALERT')
  })

  it('puts a field note behind an (i) button, closed until clicked', () => {
    const user = renderToString(<Harness part="editor" selection={{ kind: 'device', ref: 'd1' }} />)
    expect(user).toMatch(/<button type="button" class="field-note-toggle [^"]*" aria-label="Show note" aria-expanded="false"/)
    expect(user).toMatch(/<span id="[^"]+" class="field-note" role="note" hidden="">Address pins A0-A5 select the address/)
    expect(user).toMatch(/role="note" hidden="">Active-low\.<\/span>/)
  })

  it('shows only actions admitted by each importance threshold', () => {
    const labels = (importance: number) => [...renderToString(<ErrorActionsField value={[0, 0, 0, 0, 0]} actions={[]} importance={importance} onChange={() => {}} />).matchAll(/<label class="devices-error-action"><span>([^<]+)<\/span>/g)].map((match) => match[1])
    expect([0, 1, 2, 3, 4].map(labels)).toEqual([
      [],
      ['Critical'],
      ['High', 'Critical'],
      ['Medium', 'High', 'Critical'],
      ['Low', 'Medium', 'High', 'Critical'],
    ])
    const disabled = renderToString(<ErrorActionsField value={[0, 0, 0, 0, 0]} actions={[]} importance={0} onChange={() => {}} />)
    expect(disabled).toContain('Disabled ignores every error of this device, Critical included')
  })
})
