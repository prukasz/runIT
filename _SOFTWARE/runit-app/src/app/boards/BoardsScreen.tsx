import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { ArrowLeftRight, Cable, Cpu, Link2, Plug, Plus, RotateCcw, Trash2, Unplug } from 'lucide-react'
import { Sessions } from '@backend'
import type { BridgeWire, RunitLink, Services, SessionPeer } from '@backend'
import { Badge, Button, Card, CardStack, FormRow, NumberField, PanelHeader, SegmentedSwitch, TextField } from '@ui/components'
import type { useProject } from '../project/useProject'
import type { KnownBoardDevice, ProjectBoard } from '../project/projectFile'
import { boardType, nextDeviceId, SINGLE_TYPES } from '../devices/devices'
import { readBoardProfile } from '../devices/boardProfile'
import type { ProviderRegistry } from '../routing/providerRegistry'
import type { RemoteState } from '../remote/useRemote'
import { BOARD_COMMANDS, addressBytes, boardProblems, devicesOn, FIRST_LINK_PROVIDER, LAST_LINK_PROVIDER, rememberWiredBoard } from './boards'
import './Boards.css'
import type { BoardDescriptor } from '../devices/boardDescriptor'

/*
 * The Project view's Boards page: this board's id, a second board wired to it by UART (connect/detect, remembered in the
 * project once detected, with quick-add for its devices and Add device right on its card), and any BLE boards of the project.
 */

const PEERS_EVERY_MS = 3000
const UARTS = [['1', 'UART1'], ['2', 'UART2']] as const

/** The lowest board id (1 and up) neither this board nor the project's boards have. */
const nextBoardId = (boards: readonly ProjectBoard[], own: number | undefined, last: number): number => {
  const taken = new Set([...(own === undefined ? [] : [own]), ...boards.map((b) => b.id)])
  let id = 1
  while (taken.has(id) && id < last) id++
  return id
}

export function BoardsScreen({ registry, project, link, services, board, connected, remote, mainConnected, onOpenRemote, onOpenDevice, onAddDevice }: {
  registry: ProviderRegistry
  project: ReturnType<typeof useProject>
  link: RunitLink
  services: Services
  board: BoardDescriptor
  connected: boolean
  /** Present on the main board: the wired UART remote link. */
  remote?: RemoteState
  mainConnected?: boolean
  onOpenRemote?: () => void
  onOpenDevice?: (id: number) => void
  onAddDevice?: (boardId: number) => void
}) {
  const sessions = useMemo(() => new Sessions(link, services), [link, services])
  const boards = project.project.boards ?? []
  const ownId = connected ? link.session?.boardId : undefined
  const [peers, setPeers] = useState<readonly SessionPeer[]>([])
  const [address, setAddress] = useState('')
  const [note, setNote] = useState<{ readonly text: string; readonly error?: boolean }>()
  const [newOwnId, setNewOwnId] = useState<number>()

  const tell = (text: string, error = false) => setNote({ text, error })
  const failed = (e: unknown) => tell(e instanceof Error ? e.message : String(e), true)

  const readPeers = useCallback(async () => {
    if (!connected) return
    try {
      setPeers(await sessions.peers())
    } catch {
      // The next tick asks again.
    }
  }, [connected, sessions])

  useEffect(() => {
    if (!connected) return
    void readPeers()
    const timer = setInterval(() => void readPeers(), PEERS_EVERY_MS)
    return () => clearInterval(timer)
  }, [connected, readPeers])

  // Wired remote board detection: while detecting (app tunnel), read its profile, remember it as wired, then hand UART back to master.
  const remoteConnected = !!(remote && remote.mode === 'app' && remote.state === 'connected')
  const remoteSession = remoteConnected ? remote.link.session : undefined
  const [remoteProfile, setRemoteProfile] = useState<{ readonly text: string; readonly name?: string; readonly devices: readonly KnownBoardDevice[]; readonly done: boolean }>()
  const rememberedKey = useRef<string | undefined>(undefined)

  useEffect(() => {
    if (!remoteConnected || !remote) {
      setRemoteProfile(undefined)
      rememberedKey.current = undefined
      return
    }
    let alive = true
    readBoardProfile(remote.link, services.catalog).then(
      (profile) => {
        if (!alive) return
        if (!profile) {
          setRemoteProfile({ text: 'None: its view writes one', devices: [], done: true })
          return
        }
        setRemoteProfile({
          text: `${profile.name}, ${profile.devices.length} devices`,
          name: profile.name,
          devices: profile.devices.map((d) => ({ id: d.id, type: d.type, name: d.name })),
          done: true,
        })
      },
      () => { if (alive) setRemoteProfile({ text: 'Not read', devices: [], done: true }) },
    )
    return () => { alive = false }
  }, [remoteConnected, remote, services.catalog])

  // Auto-remember the detected wired board in the project, then release the tunnel so the master takes the UART session.
  useEffect(() => {
    if (!remoteConnected || !remoteSession || !remoteProfile?.done) return
    const key = `${remoteSession.boardId}:${remoteProfile.text}:${remoteProfile.devices.map((d) => `${d.id}:${d.type}`).join(',')}`
    if (rememberedKey.current !== key) {
      rememberedKey.current = key
      const updated = rememberWiredBoard(project.project, {
        id: remoteSession.boardId,
        ...(remoteProfile.name ? { name: remoteProfile.name } : {}),
        ...(remoteProfile.text ? { profile: remoteProfile.text } : {}),
        ...(remoteProfile.devices.length > 0 ? { knownDevices: remoteProfile.devices } : {}),
      })
      if (updated) project.edit(() => updated)
    }
    if (remote?.detecting) remote.finishDetect()
  }, [remoteConnected, remoteSession, remoteProfile, project, remote])

  // Also remember a wired board if the master already has an open slave session with it (e.g. after app reload).
  useEffect(() => {
    const peer = remote?.slavePeer
    if (!remote || remote.mode !== 'master' || !peer?.open || peer.peerId === undefined) return
    const updated = rememberWiredBoard(project.project, { id: peer.peerId })
    if (updated) project.edit(() => updated)
  }, [remote, project])

  const setBoards = (next: readonly ProjectBoard[], key?: string) => project.edit((p) => {
    const { boards: _dropped, ...rest } = p
    return next.length > 0 ? { ...rest, boards: next } : rest
  }, key)

  const addBleBoard = () => {
    if (!addressBytes(address)) return tell(`"${address}" is not a BLE address (six hex bytes, like F4:12:FA:01:02:03).`, true)
    const id = nextBoardId(boards, ownId, link.defs.src.boardLast)
    const provider = registry.freeFrom(FIRST_LINK_PROVIDER, LAST_LINK_PROVIDER)
    if (provider === undefined) return tell('Every provider id for a link is taken.', true)
    setBoards([...boards, { id, name: `Board ${id}`, link: 'wireless', address: address.trim().toUpperCase(), addressType: 0, provider }])
    setAddress('')
  }

  const remove = (b: ProjectBoard) => {
    const on = devicesOn(project.project, b.id)
    if (on.length > 0) return tell(`${b.name} still has ${on.map((d) => d.name).join(', ')}: move them to another board or remove them first.`, true)
    setBoards(boards.filter((o) => o.id !== b.id))
  }

  const setOwnId = async (id: number) => {
    try {
      await sessions.setBoardId(id)
      await services.call(BOARD_COMMANDS.system, BOARD_COMMANDS.restart, undefined, { side: 'write' })
      tell(`This board takes id ${id}: it restarts, the app connects again.`)
    } catch (e) {
      failed(e)
    }
  }

  // Fallback GPIO descriptor when a board's profile devices haven't been detected yet.
  const gpio = board.devices.find((d) => SINGLE_TYPES.has(boardType(services.catalog, d.descriptor)?.id ?? ''))
  const gpioType = gpio && boardType(services.catalog, gpio.descriptor)
  const addKnownDevice = (b: ProjectBoard, target: KnownBoardDevice) => {
    const id = nextDeviceId(project.project, board)
    const name = target.name.toLowerCase().includes(b.name.toLowerCase()) ? target.name : `${b.name} ${target.name}`
    project.edit((p) => ({ ...p, devices: [...p.devices, { id, type: target.type, name, board: b.id, remoteId: target.id }] }))
    tell(`${name} is in the project: set its pins in Devices and use it in Code.`)
  }

  const peerOf = (b: ProjectBoard) => (b.provider === undefined ? undefined : peers.find((p) => p.provider === b.provider))
  const problems = boardProblems(project.project, ownId, services.catalog)

  const wire = remote?.wire
  const setWire = (change: Partial<BridgeWire>) => { if (remote && wire) remote.setWire({ ...wire, ...change }) }
  const masterLinked = !!(remote && remote.mode === 'master' && remote.slavePeer?.open)
  const wireBadge = !remote?.on ? 'off' : remote.detecting ? 'detecting' : masterLinked ? 'linked to master' : remote.mode === 'master' ? 'linking' : remoteConnected ? 'connected' : 'looking'
  const wireTone = masterLinked || remoteConnected ? 'success' : remote?.on ? 'warning' : 'neutral'
  const wireError = remote?.mode === 'master' ? remote.slaveError : (!remoteConnected ? remote?.reason : undefined)

  return (
    <div className="project-screen boards-screen">
      <PanelHeader title="Boards" />
      <p className="boards-intro">Other boards of the project: connect a wired board over UART to detect and remember it, add its devices to this board&apos;s project, or switch to its view.</p>
      {note && <p className={`boards-note ${note.error ? 'is-error' : ''}`.trim()} role="status">{note.text}</p>}
      {problems.map((p) => <p key={p} className="boards-note is-error">{p}</p>)}
      <CardStack>
        <Card title="This board" icon={<Cpu aria-hidden="true" />} subhead={connected ? `Id ${ownId ?? '?'}` : 'Not connected'}>
          {ownId === 0 && <p className="boards-hint">A new board has id 0, and so does every board it finds: give this one another id before it leads boards.</p>}
          <FormRow label="Its id" note="Kept on the board, taken at its next start: it restarts.">
            <span className="boards-row">
              <NumberField aria-label="This board's id" integer min={0} max={link.defs.src.boardLast} value={newOwnId ?? ownId} onCommit={(v) => setNewOwnId(v)} />
              <Button disabled={!connected || newOwnId === undefined || newOwnId === ownId} onClick={() => newOwnId !== undefined && void setOwnId(newOwnId)}><RotateCcw aria-hidden="true" />Set and restart</Button>
            </span>
          </FormRow>
        </Card>

        {remote && wire && (
          <Card title="Wired board (UART)" icon={<Cable aria-hidden="true" />} subhead={remote.on ? 'Linked to master on this view; switches automatically when you open the remote board.' : 'Pins of this board wired to the other board\'s RX and TX.'}
            actions={<Badge tone={wireTone} size="compact">{wireBadge}</Badge>}>
            <FormRow label="UART" note="A controller of this board nothing else uses.">
              <SegmentedSwitch label="UART" value={String(wire.uart) as '1' | '2'} options={UARTS} onChange={(value) => { if (!remote.on) setWire({ uart: Number(value) }) }} />
            </FormRow>
            <FormRow label="TX pin" note="Goes to the other board's RX.">
              <NumberField aria-label="TX pin" integer min={0} max={48} value={wire.tx} disabled={remote.on} onCommit={(tx) => tx !== undefined && setWire({ tx })} />
            </FormRow>
            <FormRow label="RX pin" note="Comes from the other board's TX.">
              <NumberField aria-label="RX pin" integer min={0} max={48} value={wire.rx} disabled={remote.on} onCommit={(rx) => rx !== undefined && setWire({ rx })} />
            </FormRow>
            <FormRow label="Baud rate" note="The other board's console rate.">
              <NumberField aria-label="Baud rate" integer min={1200} max={5000000} value={wire.baud} disabled={remote.on} onCommit={(baud) => baud !== undefined && setWire({ baud })} />
            </FormRow>
            {remote.on && wireError && <p className="boards-reason" role="status">{wireError}</p>}
            <div className="project-actions">
              {remote.on
                ? <Button size="sm" variant="danger" onClick={() => void remote.disconnect()}><Unplug aria-hidden="true" />Disconnect</Button>
                : <Button size="sm" disabled={!mainConnected} title={mainConnected ? undefined : 'Connect the main board first'} onClick={() => remote.connect(wire)}><Plug aria-hidden="true" />Connect and detect</Button>}
              {remote.on && mainConnected && onOpenRemote && <Button size="sm" onClick={onOpenRemote}><ArrowLeftRight aria-hidden="true" />Show the remote board</Button>}
            </div>
          </Card>
        )}

        {boards.map((b, i) => {
          const wired = b.link === 'wired'
          const liveMaster = wired && masterLinked && remote?.slavePeer?.peerId === b.id
          const liveTunnel = wired && remoteConnected && remoteSession?.boardId === b.id
          const liveWired = liveMaster || liveTunnel
          const peer = peerOf(b)
          const state = wired
            ? (liveMaster ? 'linked to master' : liveTunnel ? 'detecting' : remote?.on ? 'linking' : 'remembered')
            : (!connected ? 'not connected' : !peer ? 'not linked' : peer.open ? 'linked' : 'linking')
          const tone = liveMaster || liveTunnel || state === 'linked' ? 'success' : state === 'linking' || state === 'detecting' ? 'warning' : 'neutral'
          const profileText = liveTunnel ? (remoteProfile?.text ?? b.profile) : b.profile
          const added = devicesOn(project.project, b.id)
          const candidates: readonly KnownBoardDevice[] = (b.knownDevices && b.knownDevices.length > 0)
            ? b.knownDevices
            : (gpio && gpioType ? [{ id: gpio.id, type: gpioType.id, name: 'GPIO' }] : [])
          const unadded = candidates.filter((kd) => !added.some((d) => d.remoteId === kd.id))
          return (
            <Card key={`${b.link ?? 'wireless'}:${b.id}:${i}`} title={b.name} icon={wired ? <Cpu aria-hidden="true" /> : <Link2 aria-hidden="true" />}
              subhead={wired ? `Id ${b.id} · Wired (UART)` : `Id ${b.id} · ${b.address ?? ''}`}
              actions={(
                <span className="boards-row">
                  <Badge tone={tone} size="compact">{state}</Badge>
                  {wired && remote?.on && mainConnected && onOpenRemote && <Button size="sm" onClick={onOpenRemote}><ArrowLeftRight aria-hidden="true" />Show the remote board</Button>}
                  {!liveWired && <Button size="icon-sm" variant="danger" aria-label="Remove the board" onClick={() => remove(b)}><Trash2 aria-hidden="true" /></Button>}
                </span>
              )}>
              <FormRow label="Name">
                <TextField aria-label="Name" value={b.name} onChange={(event) => setBoards(boards.map((o, j) => (j === i ? { ...o, name: event.target.value } : o)), `board-name:${i}`)} />
              </FormRow>
              <FormRow label="Board id" note={liveWired ? 'Detected from the board.' : 'Its id in the project.'}>
                <span className="boards-row">
                  <NumberField aria-label="Board id" integer min={0} max={link.defs.src.boardLast} value={b.id} disabled={liveWired}
                    onCommit={(v) => v !== undefined && project.edit((p) => ({
                      ...p,
                      boards: boards.map((o, j) => (j === i ? { ...o, id: v } : o)),
                      devices: p.devices.map((d) => (d.board === b.id ? { ...d, board: v } : d)),
                    }))} />
                </span>
              </FormRow>
              {profileText && (
                <FormRow label="Its profile">
                  <span>{profileText}</span>
                </FormRow>
              )}
              <FormRow label="Devices" note="Devices of this board used by this project.">
                <span className="boards-row">
                  {added.map((d) => (
                    onOpenDevice
                      ? <Button key={d.id} size="sm" onClick={() => onOpenDevice(d.id)}>{d.name}</Button>
                      : <Badge key={d.id} size="compact">{d.name}</Badge>
                  ))}
                  {unadded.map((kd) => (
                    <Button key={kd.id} size="sm" onClick={() => addKnownDevice(b, kd)}><Plus aria-hidden="true" />{kd.name}</Button>
                  ))}
                  {onAddDevice && <Button size="sm" onClick={() => onAddDevice(b.id)}><Plus aria-hidden="true" />Add device</Button>}
                </span>
              </FormRow>
            </Card>
          )
        })}

        <Card title="Add a Bluetooth board" icon={<Plus aria-hidden="true" />} subhead="Manual entry until BLE central is in the firmware.">
          <FormRow label="Its address" note="Six hex bytes, as the board shows it.">
            <span className="boards-row">
              <TextField aria-label="Board address" value={address} onChange={(event) => setAddress(event.target.value)} />
              <Button onClick={addBleBoard}><Plus aria-hidden="true" />Add</Button>
            </span>
          </FormRow>
        </Card>
      </CardStack>
    </div>
  )
}
