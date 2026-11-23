import { useCallback, useEffect, useMemo, useState } from 'react'
import { Cpu, Link2, Plus, RotateCcw, Trash2 } from 'lucide-react'
import { Sessions } from '@backend'
import type { RunitLink, Services, SessionPeer } from '@backend'
import { Badge, Button, Card, CardStack, FormRow, NumberField, PanelHeader, TextField } from '@ui/components'
import type { useProject } from '../project/useProject'
import type { ProjectBoard } from '../project/projectFile'
import { boardType, nextDeviceId, SINGLE_TYPES } from '../devices/devices'
import type { ProviderRegistry } from '../routing/providerRegistry'
import { BOARD_COMMANDS, addressBytes, boardProblems, devicesOn, FIRST_LINK_PROVIDER, LAST_LINK_PROVIDER } from './boards'
import './Boards.css'
import type { BoardDescriptor } from '../devices/boardDescriptor'

/*
 * The Project view's Boards page: the other boards of the project (boards.ts): add one by its address, name it, give it its
 * id, put its GPIO in the project (its pins are then this board's program's, through an alias). The link's state is this
 * board's PEERS. The firmware has no BLE central yet, so the page cannot find or link a board: it only keeps the project's list.
 */

const PEERS_EVERY_MS = 3000

/** The lowest board id (1 and up) neither this board nor the project's boards have. */
const nextBoardId = (boards: readonly ProjectBoard[], own: number | undefined, last: number): number => {
  const taken = new Set([...(own === undefined ? [] : [own]), ...boards.map((b) => b.id)])
  let id = 1
  while (taken.has(id) && id < last) id++
  return id
}

export function BoardsScreen({ registry, project, link, services, board, connected }: {
  registry: ProviderRegistry
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

  const setBoards = (next: readonly ProjectBoard[], key?: string) => project.edit((p) => {
    const { boards: _dropped, ...rest } = p
    return next.length > 0 ? { ...rest, boards: next } : rest
  }, key)

  const add = () => {
    if (!addressBytes(address)) return tell(`"${address}" is not a BLE address (six hex bytes, like F4:12:FA:01:02:03).`, true)
    const id = nextBoardId(boards, ownId, link.defs.src.boardLast)
    const provider = registry.freeFrom(FIRST_LINK_PROVIDER, LAST_LINK_PROVIDER)
    if (provider === undefined) return tell('Every provider id for a link is taken.', true)
    setBoards([...boards, { id, name: `Board ${id}`, address: address.trim().toUpperCase(), addressType: 0, provider }])
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

  return (
    <div className="project-screen boards-screen">
      <PanelHeader title="Boards" />
      <p className="boards-intro">Other boards of the project: this one leads them and its program uses their devices as its own. A device is put on a board in Devices. The firmware cannot find or link a board over Bluetooth yet: the list is the project's only.</p>
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

        <Card title="Add a board" icon={<Plus aria-hidden="true" />}>
          <FormRow label="Its address" note="Six hex bytes, as the board shows it.">
            <span className="boards-row">
              <TextField aria-label="Board address" value={address} onChange={(event) => setAddress(event.target.value)} />
              <Button onClick={add}><Plus aria-hidden="true" />Add</Button>
            </span>
          </FormRow>
        </Card>

        {boards.map((b, i) => {
          const peer = peerOf(b)
          const state = !connected ? 'not connected' : !peer ? 'not linked' : peer.open ? 'linked' : 'linking'
          return (
            <Card key={b.id} title={b.name} icon={<Link2 aria-hidden="true" />} subhead={`Id ${b.id} · ${b.address}`} actions={(
              <span className="boards-row">
                <Badge tone={state === 'linked' ? 'success' : state === 'linking' ? 'warning' : 'neutral'} size="compact">{state}</Badge>
                <Button size="icon-sm" variant="danger" aria-label="Remove the board" onClick={() => remove(b)}><Trash2 aria-hidden="true" /></Button>
              </span>
            )}>
              <FormRow label="Name">
                <TextField aria-label="Name" value={b.name} onChange={(event) => setBoards(boards.map((o, j) => (j === i ? { ...o, name: event.target.value } : o)), `board-name:${i}`)} />
              </FormRow>
              <FormRow label="Its id" note="Its place in the project.">
                <span className="boards-row">
                  <NumberField aria-label="Its id" integer min={0} max={link.defs.src.boardLast} value={b.id} onCommit={(v) => project.edit((p) => ({ ...p, boards: boards.map((o, j) => (j === i ? { ...o, id: v } : o)), devices: p.devices.map((d) => (d.board === b.id ? { ...d, board: v } : d)) }))} />
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
