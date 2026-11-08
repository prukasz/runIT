import { useCallback, useEffect, useMemo, useState } from 'react'
import { Cpu, Link2, Plus, RefreshCw, RotateCcw, Search, Trash2, Unlink } from 'lucide-react'
import { Sessions } from '@backend'
import type { BoardDescriptor, RunitLink, Services, SessionPeer } from '@backend'
import { Badge, Button, Card, CardStack, FormRow, NumberField, PanelHeader, TextField } from '@ui/components'
import type { useProject } from '../project/useProject'
import type { ProjectBoard } from '../project/projectFile'
import { boardType, nextDeviceId, SINGLE_TYPES } from '../devices/devices'
import { BOARD_COMMANDS, boardProblems, decodeFound, devicesOn, linkCalls, nextLinkProvider, SCAN_MS } from './boards'
import type { FoundBoard } from './boards'
import './Boards.css'

/*
 * The Project view's Boards page: the other boards of the project, linked to the one the app is connected to over BLE
 * (boards.ts). Find them (this board scans), add one (it is linked at once and at every start once written), give it its id
 * (through this board: SESSION ID there, then its restart), put its GPIO in the project (its pins are then this board's
 * program's, through an alias). The link's state is this board's PEERS.
 */

const PEERS_EVERY_MS = 3000
const FOUND_EVERY_MS = 500

/** The lowest board id (1 and up) neither this board nor the project's boards have. */
const nextBoardId = (boards: readonly ProjectBoard[], own: number | undefined, last: number): number => {
  const taken = new Set([...(own === undefined ? [] : [own]), ...boards.map((b) => b.id)])
  let id = 1
  while (taken.has(id) && id < last) id++
  return id
}

export function BoardsScreen({ project, link, services, board, connected }: {
  project: ReturnType<typeof useProject>
  link: RunitLink
  services: Services
  board: BoardDescriptor
  connected: boolean
}) {
  const sessions = useMemo(() => new Sessions(link, services), [link, services])
  const boards = project.project.boards ?? []
  const ownId = connected ? link.session?.boardId : undefined
  const [peers, setPeers] = useState<readonly SessionPeer[]>([])
  const [found, setFound] = useState<readonly FoundBoard[]>([])
  const [scanning, setScanning] = useState(false)
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

  const setBoards = (next: readonly ProjectBoard[], key?: string) => project.edit((p) => {
    const { boards: _dropped, ...rest } = p
    return next.length > 0 ? { ...rest, boards: next } : rest
  }, key)

  const scan = async () => {
    try {
      setFound([])
      setScanning(true)
      await services.call(BOARD_COMMANDS.ble, BOARD_COMMANDS.scan, { duration_ms: SCAN_MS }, { side: 'write' })
      const until = Date.now() + SCAN_MS + 2000
      for (;;) {
        await new Promise((r) => setTimeout(r, FOUND_EVERY_MS))
        const { data } = await services.call(BOARD_COMMANDS.ble, BOARD_COMMANDS.found, undefined, { side: 'read' })
        const answer = decodeFound(data)
        setFound(answer.boards)
        if (!answer.scanning || Date.now() > until) break
      }
    } catch (e) {
      failed(e)
    } finally {
      setScanning(false)
    }
  }

  // The link now: BLE CONNECT and SESSION ATTACH (already there: the board takes them as done).
  const linkNow = async (b: ProjectBoard) => {
    try {
      for (const call of linkCalls(link.defs, services.catalog, b)) await services.send(call)
      tell(`${b.name}: linking. It shows as linked once its hello came.`)
      void readPeers()
    } catch (e) {
      failed(e)
    }
  }

  const add = (f: FoundBoard) => {
    const id = nextBoardId(boards, ownId, link.defs.session.id.boardLast)
    const b: ProjectBoard = { id, name: `Board ${id}`, address: f.address, addressType: f.addressType, provider: nextLinkProvider(project.project) }
    setBoards([...boards, b])
    if (connected) void linkNow(b)
  }

  const unlink = async (b: ProjectBoard) => {
    try {
      await services.call(BOARD_COMMANDS.session, BOARD_COMMANDS.detach, { provider: b.provider }, { side: 'write' })
      await services.call(BOARD_COMMANDS.provider, BOARD_COMMANDS.remove, { provider: b.provider }, { side: 'write' })
      tell(`${b.name}: unlinked until the project is written again.`)
      void readPeers()
    } catch (e) {
      failed(e)
    }
  }

  const remove = (b: ProjectBoard) => {
    const on = devicesOn(project.project, b.id)
    if (on.length > 0) return tell(`${b.name} still has ${on.map((d) => d.name).join(', ')}: move them to another board or remove them first.`, true)
    setBoards(boards.filter((o) => o.id !== b.id))
    if (connected) void unlink(b)
  }

  // The board answers with the id it has now (a new board: 0); it takes the project's at its next start.
  const giveId = async (b: ProjectBoard, now: number) => {
    try {
      await sessions.setBoardId(b.id, { board: now })
      await services.call(BOARD_COMMANDS.system, BOARD_COMMANDS.restart, undefined, { side: 'write', board: now })
      tell(`${b.name}: id ${b.id} given; it restarts and comes back with it.`)
    } catch (e) {
      failed(e)
    }
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

  // The board's GPIO (the chip's own, which every board has) as a device of the project on board `b`.
  const gpio = board.devices.find((d) => SINGLE_TYPES.has(boardType(services.catalog, d.descriptor)?.id ?? ''))
  const gpioType = gpio && boardType(services.catalog, gpio.descriptor)
  const addGpio = (b: ProjectBoard) => {
    if (!gpio || !gpioType) return
    project.edit((p) => ({ ...p, devices: [...p.devices, { id: nextDeviceId(p, board), type: gpioType.id, name: `${b.name} GPIO`, board: b.id, remoteId: gpio.id }] }))
    tell(`${b.name} GPIO is in the project: set its pins in Devices, use them in Code; Write puts it on both boards.`)
  }

  const peerOf = (b: ProjectBoard) => peers.find((p) => p.provider === b.provider)
  const problems = boardProblems(project.project, ownId, services.catalog)
  const known = new Set(boards.map((b) => b.address))

  return (
    <div className="project-screen boards-screen">
      <PanelHeader title="Boards" />
      <p className="boards-intro">Other boards this one leads over Bluetooth: it links to each, the app reaches them through it, and its program uses their devices as its own. A device is put on a board in Devices.</p>
      {note && <p className={`boards-note ${note.error ? 'is-error' : ''}`.trim()} role="status">{note.text}</p>}
      {problems.map((p) => <p key={p} className="boards-note is-error">{p}</p>)}
      <CardStack>
        <Card title="This board" icon={<Cpu aria-hidden="true" />} subhead={connected ? `Id ${ownId ?? '?'}` : 'Not connected'}>
          {ownId === 0 && <p className="boards-hint">A new board has id 0, and so does every board it finds: give this one another id before it leads boards.</p>}
          <FormRow label="Its id" note="Kept on the board, taken at its next start: it restarts.">
            <span className="boards-row">
              <NumberField aria-label="This board's id" integer min={0} max={link.defs.session.id.boardLast} value={newOwnId ?? ownId} onCommit={(v) => setNewOwnId(v)} />
              <Button disabled={!connected || newOwnId === undefined || newOwnId === ownId} onClick={() => newOwnId !== undefined && void setOwnId(newOwnId)}><RotateCcw aria-hidden="true" />Set and restart</Button>
            </span>
          </FormRow>
        </Card>

        <Card title="Find boards" icon={<Search aria-hidden="true" />} actions={<Button disabled={!connected || scanning} onClick={() => void scan()}><RefreshCw aria-hidden="true" />{scanning ? 'Scanning…' : 'Scan'}</Button>}>
          {found.length === 0 && <p className="boards-hint">{scanning ? 'Looking for runIT boards near this one…' : 'Scan to list the runIT boards this board sees.'}</p>}
          {found.map((f) => (
            <FormRow key={f.address} label={`${f.name || 'runIT'} · ${f.address}`} note={`${f.rssi} dBm`}>
              {known.has(f.address)
                ? <Badge tone="success" size="compact">in the project</Badge>
                : <Button onClick={() => add(f)}><Plus aria-hidden="true" />Add</Button>}
            </FormRow>
          ))}
        </Card>

        {boards.map((b, i) => {
          const peer = peerOf(b)
          const state = !connected ? 'not connected' : !peer ? 'not linked' : peer.open ? 'linked' : 'linking'
          const nowId = peer?.open ? peer.peerId : undefined
          return (
            <Card key={b.id} title={b.name} icon={<Link2 aria-hidden="true" />} subhead={`Id ${b.id} · ${b.address}`} actions={(
              <span className="boards-row">
                <Badge tone={state === 'linked' ? 'success' : state === 'linking' ? 'warning' : 'neutral'} size="compact">{state}</Badge>
                <Button size="icon-sm" aria-label="Link now" title="Link it now (Write keeps it linked at every start)" disabled={!connected} onClick={() => void linkNow(b)}><Link2 aria-hidden="true" /></Button>
                <Button size="icon-sm" aria-label="Unlink" title="End the link now" disabled={!connected || !peer} onClick={() => void unlink(b)}><Unlink aria-hidden="true" /></Button>
                <Button size="icon-sm" variant="danger" aria-label="Remove the board" onClick={() => remove(b)}><Trash2 aria-hidden="true" /></Button>
              </span>
            )}>
              <FormRow label="Name">
                <TextField aria-label="Name" value={b.name} onChange={(event) => setBoards(boards.map((o, j) => (j === i ? { ...o, name: event.target.value } : o)), `board-name:${i}`)} />
              </FormRow>
              <FormRow label="Its id" note={nowId !== undefined && nowId !== b.id ? `It has id ${nowId} now: give it the project's, it restarts.` : 'Its place in the project; the app reaches it by it.'}>
                <span className="boards-row">
                  <NumberField aria-label="Its id" integer min={0} max={link.defs.session.id.boardLast} value={b.id} onCommit={(v) => project.edit((p) => ({ ...p, boards: boards.map((o, j) => (j === i ? { ...o, id: v } : o)), devices: p.devices.map((d) => (d.board === b.id ? { ...d, board: v } : d)) }))} />
                  {nowId !== undefined && nowId !== b.id && <Button onClick={() => void giveId(b, nowId)}><RotateCcw aria-hidden="true" />Give it id {b.id}</Button>}
                </span>
              </FormRow>
              <FormRow label="Devices" note="Devices of the project on this board.">
                <span className="boards-row">
                  {devicesOn(project.project, b.id).map((d) => <Badge key={d.id} size="compact">{d.name}</Badge>)}
                  {gpio && !devicesOn(project.project, b.id).some((d) => d.remoteId === gpio.id) && <Button onClick={() => addGpio(b)}><Plus aria-hidden="true" />Its GPIO</Button>}
                </span>
              </FormRow>
            </Card>
          )
        })}
      </CardStack>
    </div>
  )
}
