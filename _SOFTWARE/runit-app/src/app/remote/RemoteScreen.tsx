import { useEffect, useMemo, useState } from 'react'
import { ArrowLeftRight, Cable, Cpu, Plug, Unplug } from 'lucide-react'
import { bundledServiceCatalog } from '@backend'
import type { BridgeWire, RunitLink } from '@backend'
import { Badge, Button, Card, CardStack, FormRow, NumberField, PanelHeader, SegmentedSwitch } from '@ui/components'
import { readBoardProfile } from '../devices/boardProfile'
import type { RemoteState } from './useRemote'
import './Remote.css'

/*
 * The Project view's Create remote page: another board wired to the main one by a UART. Connect makes the way through the main
 * board and opens a session with the other board; once it is there the app can show it instead of the main board (the right
 * toolbar's switch).
 */

const UARTS = [['1', 'UART1'], ['2', 'UART2']] as const

const hex = (value: number): string => `0x${value.toString(16).padStart(8, '0')}`

/** The profile the remote board keeps, by name and the number of its devices, once it is there. */
const useRemoteProfile = (link: RunitLink, connected: boolean): string => {
  const catalog = useMemo(bundledServiceCatalog, [])
  const [text, setText] = useState('…')
  useEffect(() => {
    setText('…')
    if (!connected) return
    let alive = true
    readBoardProfile(link, catalog).then(
      (profile) => { if (alive) setText(profile ? `${profile.name}, ${profile.devices.length} devices` : 'None: its view writes one') },
      () => { if (alive) setText('Not read') },
    )
    return () => { alive = false }
  }, [link, catalog, connected])
  return text
}

export function RemoteScreen({ remote, main, mainConnected, onOpen }: {
  remote: RemoteState
  main: RunitLink
  mainConnected: boolean
  /** Shows the remote board instead of the main one. */
  onOpen: () => void
}) {
  const connected = remote.on && remote.state === 'connected'
  const session = connected ? remote.link.session : undefined
  const profile = useRemoteProfile(remote.link, connected)
  const wire = remote.wire
  const set = (change: Partial<BridgeWire>) => remote.setWire({ ...wire, ...change })
  const badge = connected ? 'connected' : !remote.on ? 'off' : 'looking'
  const sameBuild = session && main.session ? session.buildId === main.session.buildId : undefined

  return (
    <div className="project-screen remote-screen">
      <PanelHeader title="Create remote" />
      <p className="remote-intro">A second board wired to this one by a UART, from this board&apos;s pins to the RX and TX of the other (its console). The app talks to it through this board, in a session of its own, while it stays connected to this one over Bluetooth.</p>
      <CardStack>
        <Card title="The wire" icon={<Cable aria-hidden="true" />} subhead={remote.on ? 'Disconnect to change it.' : 'Pins of this board.'}>
          <FormRow label="UART" note="A controller of this board nothing else uses.">
            <SegmentedSwitch label="UART" value={String(wire.uart) as '1' | '2'} options={UARTS} onChange={(value) => { if (!remote.on) set({ uart: Number(value) }) }} />
          </FormRow>
          <FormRow label="TX pin" note="Goes to the other board's RX.">
            <NumberField aria-label="TX pin" integer min={0} max={48} value={wire.tx} disabled={remote.on} onCommit={(tx) => tx !== undefined && set({ tx })} />
          </FormRow>
          <FormRow label="RX pin" note="Comes from the other board's TX.">
            <NumberField aria-label="RX pin" integer min={0} max={48} value={wire.rx} disabled={remote.on} onCommit={(rx) => rx !== undefined && set({ rx })} />
          </FormRow>
          <FormRow label="Baud rate" note="The other board's console rate.">
            <NumberField aria-label="Baud rate" integer min={1200} max={5000000} value={wire.baud} disabled={remote.on} onCommit={(baud) => baud !== undefined && set({ baud })} />
          </FormRow>
        </Card>

        <Card title="The remote board" icon={<Cpu aria-hidden="true" />} actions={<Badge tone={connected ? 'success' : remote.on ? 'warning' : 'neutral'} size="compact">{badge}</Badge>}>
          {session && (
            <>
              <FormRow label="Board id"><span>{session.boardId}</span></FormRow>
              <FormRow label="Firmware"><span>{hex(session.buildId)}{sameBuild === undefined ? '' : sameBuild ? ', the same build as this board' : ', another build than this board'}</span></FormRow>
              <FormRow label="Its profile"><span>{profile}</span></FormRow>
            </>
          )}
          {remote.on && !connected && remote.reason && <p className="remote-reason" role="status">{remote.reason}</p>}
          <div className="project-actions">
            {remote.on
              ? <Button size="sm" variant="danger" onClick={() => void remote.disconnect()}><Unplug aria-hidden="true" />Disconnect</Button>
              : <Button size="sm" disabled={!mainConnected} title={mainConnected ? undefined : 'Connect the main board first'} onClick={() => remote.connect(wire)}><Plug aria-hidden="true" />Connect and detect</Button>}
            {connected && <Button size="sm" onClick={onOpen}><ArrowLeftRight aria-hidden="true" />Show the remote board</Button>}
          </div>
        </Card>
      </CardStack>
    </div>
  )
}
