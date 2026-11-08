import { Plus, Trash2 } from 'lucide-react'
import type { FieldValues } from '@backend'
import { Button, Card, CardStack, FormGrid, FormRow, NumberField, PanelHeader, Switch } from '@ui/components'
import type { ProfileI2cBus } from '../project/projectFile'
import { FieldsForm } from './FieldsForm'
import { SINGLE_TYPES } from './devices'
import type { DevicesState } from './useDevices'

/*
 * The Project view's Buses tab: the I2C buses and the UART ports the board creates at every load, kept in its profile
 * (devices/boardEditor.ts) with its devices. The record creates them first, before any device (SYS_BOARD.MD), on pins of the chip's
 * own GPIO. A UART port is its `UART CREATE` arguments (SYS_UART.MD), set with the command's own form. `state` is the board editor's
 * (`useDevices(..., 'board')`): Write puts them on the board with the devices.
 */

const PIN_MAX = 48
/** The UARTs CREATE may take: the console has UART 0 (SYS_UART.MD). */
const UART_PORTS = [2, 1]
/** The provider ids a board port starts from: above the board's own (1..4), below the project's (0x10..). */
const FIRST_PROVIDER = 5

/** The lowest bus number nothing uses. */
const freeBus = (buses: readonly ProfileI2cBus[]): number => {
  let bus = 0
  while (buses.some((b) => b.bus === bus)) bus++
  return bus
}

export function BusesScreen({ state }: { state: DevicesState }) {
  const buses = state.project.i2cBuses ?? []
  const uarts = state.project.uartPorts ?? []
  const gpio = state.devices.find((d) => d.type !== undefined && SINGLE_TYPES.has(d.type.id))
  const uartCreate = state.catalog.services.get('UART')?.commands.get('CREATE')?.write?.args
  const context = { devices: state.devices, board: state.board }

  const changeBus = (bus: number, next: (b: ProfileI2cBus) => ProfileI2cBus) => state.setBuses((all) => all.map((b) => (b.bus === bus ? next(b) : b)))
  const addBus = () => {
    if (!gpio) return
    state.setBuses((all) => {
      const bus = freeBus(all)
      // Free pins: no bus has them, no device uses them, the board keeps none of them.
      const taken = new Set([...all.flatMap((b) => [b.sda.pin, b.scl.pin]), ...state.devices.flatMap((d) => d.uses.filter((u) => u.pin.device === gpio.id).map((u) => u.pin.pin))])
      const pins = Array.from({ length: PIN_MAX + 1 }, (_, i) => i).filter((p) => !taken.has(p) && !state.board.reservedPins.some((r) => r.device === gpio.id && r.pin === p))
      return [...all, { bus, name: `Bus ${bus}`, sda: { device: gpio.id, pin: pins[0] ?? 0 }, scl: { device: gpio.id, pin: pins[1] ?? 1 }, pullup: false }]
    })
  }

  const addUart = () => {
    if (!uartCreate) return
    state.setUarts((all) => {
      const port = UART_PORTS.find((p) => !all.some((u) => u.port === p)) ?? UART_PORTS[0]!
      let provider = FIRST_PROVIDER
      while (all.some((u) => u.provider === provider)) provider++
      const defaults = Object.fromEntries(uartCreate.fields.filter((f) => f.default !== undefined).map((f) => [f.id, f.default])) as FieldValues
      // The fields with no default in the descriptor: the first mode and parity, no pins yet (the user picks TX and RX).
      return [...all, { mode: 0, parity: 0, tx: null, rx: null, rts: null, cts: null, ...defaults, port, provider }]
    })
  }

  return (
    <div className="project-screen">
      <PanelHeader title="Buses">
        <Button size="sm" disabled={!gpio} onClick={addBus}><Plus aria-hidden="true" />I2C bus</Button>
        <Button size="sm" disabled={!uartCreate} onClick={addUart}><Plus aria-hidden="true" />UART port</Button>
      </PanelHeader>
      <p className="project-note">The I2C buses and UART ports the board creates every time it loads, before its devices, on pins of {gpio?.name ?? 'the chip'}. Kept with the board devices: Write puts both on the board.</p>
      <CardStack>
        {buses.length === 0 && uarts.length === 0 && <Card title="No buses" subhead="Add an I2C bus or a UART port above." />}
        {[...buses].sort((a, b) => a.bus - b.bus).map((b) => (
          <Card key={`i2c:${b.bus}`} title={`I2C bus ${b.bus}`} {...(b.description ? { subhead: b.description } : {})}
            actions={<Button size="icon-sm" variant="danger" aria-label={`Remove I2C bus ${b.bus}`} title="Remove this bus from the board (devices on it keep their bus number)" onClick={() => state.setBuses((all) => all.filter((x) => x.bus !== b.bus))}><Trash2 aria-hidden="true" /></Button>}>
            <FormGrid>
              <FormRow label="SDA pin"><NumberField aria-label={`Bus ${b.bus} SDA pin`} value={b.sda.pin} min={0} max={PIN_MAX} integer onCommit={(pin) => changeBus(b.bus, (x) => ({ ...x, sda: { ...x.sda, pin } }))} /></FormRow>
              <FormRow label="SCL pin"><NumberField aria-label={`Bus ${b.bus} SCL pin`} value={b.scl.pin} min={0} max={PIN_MAX} integer onCommit={(pin) => changeBus(b.bus, (x) => ({ ...x, scl: { ...x.scl, pin } }))} /></FormRow>
              <FormRow label="Pull-ups"><Switch label={b.pullup ? 'On' : 'Off'} checked={b.pullup} onChange={(pullup) => changeBus(b.bus, (x) => ({ ...x, pullup }))} /></FormRow>
            </FormGrid>
          </Card>
        ))}
        {uarts.map((u, i) => (
          <Card key={`uart:${i}`} title={`UART ${String(u.port ?? '?')}`} subhead={`Provider ${String(u.provider ?? '?')}: a session, a stream or a device reaches the port by it.`}
            actions={<Button size="icon-sm" variant="danger" aria-label={`Remove UART ${String(u.port)}`} title="Remove this port from the board" onClick={() => state.setUarts((all) => all.filter((_, j) => j !== i))}><Trash2 aria-hidden="true" /></Button>}>
            {uartCreate && <FieldsForm layout={uartCreate} values={u} context={context} onChange={(next) => state.setUarts((all) => all.map((x, j) => (j === i ? next : x)))} />}
          </Card>
        ))}
      </CardStack>
    </div>
  )
}
