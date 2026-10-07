import { renderToString } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { defaultInstall } from '../../../src/domain/devices'
import type { ProjectDevice } from '../../../src/domain/project'
import { defaultProjectSettings } from '../../../src/domain/settings'
import { BoardCanvas } from '../../../src/devices/BoardCanvas'
import { BoardDeviceDetails } from '../../../src/devices/BoardDeviceDetails'
import { BoardToolbar } from '../../../src/devices/BoardToolbar'
import { useBoardWiring } from '../../../src/devices/useBoardWiring'
import { useDevicesWorkspace } from '../../../src/devices/useDevicesWorkspace'
import type { DevicesWorkspace } from '../../../src/devices/useDevicesWorkspace'

const catalog = runitDeviceCatalog()
const pca = catalog.type('device_pca9685')!
const device: ProjectDevice = { id: 'd1', deviceId: 20, type: pca.id, name: 'arm', tags: [], install: defaultInstall(pca, catalog.i2cBuses.user) }

function Harness({ placed, selected }: { placed?: boolean; selected?: boolean }) {
  const base = useDevicesWorkspace()
  const w: DevicesWorkspace = {
    ...base,
    devices: [device],
    boardLayout: placed ? { d1: { x: 5, y: 7, rotation: 90, mirror: true } } : undefined,
    selection: selected ? { kind: 'device', ref: 'd1' } : undefined,
  }
  const wiring = useBoardWiring()
  return <BoardCanvas workspace={w} wiring={wiring} />
}

describe('BoardCanvas', () => {
  it('renders the board with its terminals', () => {
    const html = renderToString(<Harness />)
    expect(html).toContain('board-art')
    expect(html).toContain('id="i2c-sda"')
  })

  it('draws a device from its generated symbol, scoped, at its placement', () => {
    const html = renderToString(<Harness placed selected />)
    expect(html).toContain('data-device="d1"')
    expect(html).toContain('id="pin-SDA"')
    expect(html).toContain('translate(100px, 140px) rotate(90deg) scaleX(-1)')
    expect(html).toContain('is-selected')
    // The rotate / mirror tools are in the top toolbar, not on the canvas surface (a press there deselects).
    expect(html).not.toContain('Rotate')
  })
})

describe('BoardToolbar', () => {
  function Tools({ selected }: { selected?: boolean }) {
    const base = useDevicesWorkspace()
    const w: DevicesWorkspace = { ...base, devices: [device], selection: selected ? { kind: 'device', ref: 'd1' } : undefined }
    const wiring = useBoardWiring()
    return <BoardToolbar workspace={w} wiring={wiring} settings={defaultProjectSettings()} board={{ reading: false, refresh: () => {} }} />
  }

  it('rotate and mirror need a selected device, delete a selected wire', () => {
    // Rotate, mirror and delete wire are all disabled with nothing selected; a selected device enables the first two.
    const disabled = (html: string, label: string) => new RegExp(`aria-label="${label.replace(/[()]/g, '[$&]')}"[^>]*disabled=""`).test(html)
    const idle = renderToString(<Tools />)
    for (const label of ['Rotate (R)', 'Mirror (M)', 'Delete wire']) expect(disabled(idle, label)).toBe(true)
    const enabled = renderToString(<Tools selected />)
    expect(disabled(enabled, 'Rotate (R)')).toBe(false)
    expect(disabled(enabled, 'Mirror (M)')).toBe(false)
    expect(disabled(enabled, 'Delete wire')).toBe(true)
    expect(enabled).toContain('Rotate 90° (R)')
    expect(enabled).toContain('Mirror (M)')
    expect(enabled).toContain('Click a wire to select it')
  })

  it('sends and saves the hardware config only with something to send', () => {
    const html = renderToString(<Tools />)
    expect(html).toContain('aria-label="Install all devices"')
    expect(html).toContain('aria-label="Apply all default settings"')
    expect(html).toContain('aria-label="Save hardware config to the board"')
    // The project has a device but no default settings: nothing to apply.
    expect(new RegExp('aria-label="Apply all default settings"[^>]*disabled=""').test(html)).toBe(true)
    expect(new RegExp('aria-label="Install all devices"[^>]*disabled=""').test(html)).toBe(false)
  })

  it('offers the five wiring view options', () => {
    const html = renderToString(<Tools />)
    for (const text of ['Power', 'IO', 'Device', 'Volts', 'Available']) expect(html).toContain(text)
  })
})

describe('BoardDeviceDetails', () => {
  it('lists the variants of the selected device and marks the one in use', () => {
    function Panel({ variant }: { variant?: string }) {
      const base = useDevicesWorkspace()
      const w: DevicesWorkspace = { ...base, devices: [device], boardLayout: variant ? { d1: { x: 0, y: 0, rotation: 0, mirror: false, variant } } : undefined, selection: { kind: 'device', ref: 'd1' } }
      const wiring = useBoardWiring()
      return <BoardDeviceDetails workspace={w} wiring={wiring} onEditComponent={() => {}} onEditBoard={() => {}} />
    }
    const html = renderToString(<Panel variant="breakout" />)
    expect(html).toContain('Breakout board')
    expect(html).toContain('not brought out: EXTCLK')
    expect(html).toMatch(/aria-checked="true"[^>]*class="is-selected"[^>]*>(?:(?!<\/button>).)*Breakout board/s)
    expect(renderToString(<Panel />)).toMatch(/aria-checked="true"[^>]*class="is-selected"[^>]*>(?:(?!<\/button>).)*Chip/s)
  })

  it('puts Edit component on the device page and Edit board on the board page', () => {
    function Panel({ selection }: { selection: DevicesWorkspace['selection'] }) {
      const base = useDevicesWorkspace()
      const w: DevicesWorkspace = { ...base, devices: [device], selection }
      const wiring = useBoardWiring()
      return <BoardDeviceDetails workspace={w} wiring={wiring} onEditComponent={() => {}} onEditBoard={() => {}} />
    }
    const deviceHtml = renderToString(<Panel selection={{ kind: 'device', ref: 'd1' }} />)
    expect(deviceHtml).toContain('Edit component')
    expect(deviceHtml).not.toContain('Edit board')
    const boardHtml = renderToString(<Panel selection={{ kind: 'board' }} />)
    expect(boardHtml).toContain('Edit board')
    expect(boardHtml).not.toContain('Edit component')
    expect(renderToString(<Panel selection={undefined} />)).not.toContain('Edit board')
  })
})
