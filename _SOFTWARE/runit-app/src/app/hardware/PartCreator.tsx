import { useMemo, useRef, useState } from 'react'
import type { PointerEvent, WheelEvent } from 'react'
import { Maximize, Minus, Plus, Trash2, X } from 'lucide-react'
import type { BoardDescriptor, ServiceCatalog } from '@backend'
import { Badge, Button, Card, CardStack, ChoiceField, FieldError, FormGrid, FormRow, NumberField, PanelHeader, SegmentedSwitch, Slider, Switch, TextField } from '@ui/components'
import { desktop } from '../desktop'
import { ConnectorShape } from './ConnectorShape'
import { CONNECTOR_LABELS, buildSvg, draftOf, draftProblems, newConnector, newDraft, partFiles, roleChoices, roleOf, slug, withPinCount, withRole } from './creator'
import type { Draft } from './creator'
import { connectorBody, connectorPinPoints, fitBox, rulerCm, screenToCanvas, SCREW_PITCHES, zoomAt } from './geometry'
import type { Viewport } from './geometry'
import { maskOf, pixelsUrl, readImage, vectorize } from './imageTrace'
import type { Pixels } from './imageTrace'
import { CATEGORY_LABELS, svgUrl } from './library'
import type { ChipPlacement, Connector, ConnectorType, ElementKind, NetRole, Part, PartCategory, PartNet, Point, PowerElement, Rotation } from './library'
import type { HardwareState } from './useHardware'

/*
 * The part creator (the pen of the Devices canvas): a devkit, a module, a sensor, an actuator, a chip or a whole board, as simple
 * as it can be. Its body is a plain shape, or a photo: the background taken away by clicks on it, then traced into a few flat
 * colours, at the size the user gives in millimetres. Connectors are the standard ones at their true size (goldpins always on
 * 2.54 mm) placed by dragging; each pin is given what it is from one list (ground, a power net, a pin of the part's device, the
 * pin a config field takes, a bus line, a motor lead; on a board a pin of a board device). The power nets say what the part
 * wants and draws; a board also gets its flow of energy and its chips (the ESP32-S3 module from the library among them).
 * Used in the project at once; saved into the library (_SOFTWARE/library/parts/<id>/) as a template.
 */

const SNAP = 0.635 // a quarter of the goldpin pitch
const ROTATIONS = [['0', '0°'], ['90', '90°'], ['180', '180°'], ['270', '270°']] as const
const CONNECTOR_TYPES: readonly ConnectorType[] = ['goldpin', 'goldpin_female', 'screw_terminal', 'wire', 'xt60', 'usb_c', 'pad']
const ROLES: readonly NetRole[] = ['sink', 'source', 'through', 'internal']
const ELEMENT_KINDS: readonly ElementKind[] = ['source', 'fuse', 'regulator', 'mux', 'load']

interface Photo {
  readonly pixels: Pixels
  readonly seeds: readonly Point[]
  readonly tolerance: number
  readonly colours: number
  readonly detail: number
  readonly width_mm: number
}

const download = (name: string, text: string, type: string) => {
  const a = document.createElement('a')
  a.href = URL.createObjectURL(new Blob([text], { type }))
  a.download = name
  a.click()
  setTimeout(() => URL.revokeObjectURL(a.href), 1000)
}

const maskBox = (mask: Uint8Array | undefined, w: number, h: number) => {
  if (!mask) return { x0: 0, y0: 0, x1: w - 1, y1: h - 1 }
  let [x0, y0, x1, y1] = [w, h, -1, -1]
  for (let i = 0; i < mask.length; i++) if (mask[i]) { const x = i % w; const y = Math.floor(i / w); x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y) }
  return x1 < 0 ? { x0: 0, y0: 0, x1: w - 1, y1: h - 1 } : { x0, y0, x1, y1 }
}

export function PartCreator({ state, catalog, board }: { state: HardwareState; catalog: ServiceCatalog; board: BoardDescriptor }) {
  const [draft, setDraft] = useState<Draft>(() => (state.creator?.part ? draftOf(state.creator.part) : newDraft()))
  const [idTouched, setIdTouched] = useState(!!state.creator?.part)
  const [selected, setSelected] = useState<string | undefined>()
  const [photo, setPhoto] = useState<Photo | undefined>()
  const [view, setView] = useState<Viewport | undefined>()
  const [message, setMessage] = useState<string | undefined>()
  const stage = useRef<HTMLDivElement>(null)
  const drag = useRef<{ kind: 'pan'; start: Point; view: Viewport; moved: boolean } | { kind: 'connector'; id: string; start: Point; origin: Point; moved: boolean } | undefined>(undefined)

  const part = draft.part
  const setPart = (fn: (p: Part) => Part) => setDraft((d) => ({ ...d, part: fn(d.part) }))
  const type = part.device ? catalog.devices.get(part.device.type) : undefined
  const boardDevices = useMemo(() => state.ctx.devices.filter((d) => d.system), [state.ctx.devices])
  const roles = useMemo(() => roleChoices(part, type, boardDevices), [part, type, boardDevices])
  const svg = useMemo(() => buildSvg(draft), [draft])
  const problems = draftProblems(draft, state.library)
  const connector = part.connectors.find((c) => c.id === selected)
  const setConnector = (id: string, fn: (c: Connector) => Connector) => setPart((p) => ({ ...p, connectors: p.connectors.map((c) => (c.id === id ? fn(c) : c)) }))

  // The photo: its mask from the clicks, and where it sits (its part's left and top edge at 0, at the given width).
  const mask = useMemo(() => (photo ? maskOf(photo.pixels, photo.seeds, photo.tolerance) : undefined), [photo])
  const photoUrl = useMemo(() => (photo ? pixelsUrl(photo.pixels, mask) : undefined), [photo, mask])
  const box = photo ? maskBox(mask, photo.pixels.width, photo.pixels.height) : undefined
  const mmPerPx = photo && box ? photo.width_mm / (box.x1 - box.x0 + 1) : 1

  const v = view ?? { x: 40, y: 40, zoom: 8 }
  const local = (e: { clientX: number; clientY: number }): Point => {
    const r = stage.current!.getBoundingClientRect()
    return { x: e.clientX - r.left, y: e.clientY - r.top }
  }
  const fit = () => {
    const r = stage.current?.getBoundingClientRect()
    if (r) setView(fitBox({ x: -5, y: -5, w: part.size_mm.w + 10, h: part.size_mm.h + 10 }, { w: r.width, h: r.height }, 30))
  }
  const onPointerDown = (e: PointerEvent<HTMLDivElement>) => {
    const target = e.target as Element
    stage.current?.setPointerCapture(e.pointerId)
    const id = target.closest('[data-connector]')?.getAttribute('data-connector')
    const at = screenToCanvas(v, local(e))
    if (id) {
      const c = part.connectors.find((x) => x.id === id)!
      setSelected(id)
      drag.current = { kind: 'connector', id, start: at, origin: c.at, moved: false }
    } else if (photo && target.closest('[data-photo]') && box) {
      // A click on the photo: that colour round there is background.
      setPhoto({ ...photo, seeds: [...photo.seeds, { x: at.x / mmPerPx + box.x0, y: at.y / mmPerPx + box.y0 }] })
    } else drag.current = { kind: 'pan', start: local(e), view: v, moved: false }
  }
  const onPointerMove = (e: PointerEvent<HTMLDivElement>) => {
    const d = drag.current
    if (!d) return
    if (d.kind === 'pan') {
      const p = local(e)
      d.moved = true
      setView({ ...d.view, x: d.view.x + p.x - d.start.x, y: d.view.y + p.y - d.start.y })
    } else {
      const p = screenToCanvas(v, local(e))
      const snap = (n: number) => (e.altKey ? Math.round(n * 100) / 100 : Math.round(n / SNAP) * SNAP)
      const to = { x: snap(d.origin.x + p.x - d.start.x), y: snap(d.origin.y + p.y - d.start.y) }
      setConnector(d.id, (c) => ({
        ...c, at: { x: Math.round(to.x * 1000) / 1000, y: Math.round(to.y * 1000) / 1000 },
        ...(c.type === 'pad' ? { pins: c.pins.map((pin) => (pin.at ? { ...pin, at: { x: pin.at.x + to.x - c.at.x, y: pin.at.y + to.y - c.at.y } } : pin)) } : {}),
      }))
    }
  }
  const onPointerUp = () => {
    const d = drag.current
    drag.current = undefined
    if (d?.kind === 'pan' && !d.moved) setSelected(undefined)
  }
  const onWheel = (e: WheelEvent<HTMLDivElement>) => setView(zoomAt(v, e.deltaY < 0 ? 1.12 : 1 / 1.12, local(e)))

  const addConnector = (t: ConnectorType) => {
    // Each new one a row lower than the last, so none lands on another.
    const row = part.connectors.length * 2 * 2.54
    const c = newConnector(part, t, { x: 2 * 2.54, y: Math.round(((row % Math.max(2.54, part.size_mm.h - 2.54)) + 2.54) / 0.635) * 0.635 })
    setPart((p) => ({ ...p, connectors: [...p.connectors, c] }))
    setSelected(c.id)
  }
  const trace = () => {
    if (!photo || !mask) return
    const traced = vectorize(photo.pixels, mask, { colours: photo.colours, width_mm: photo.width_mm, detail: photo.detail })
    if (!traced) return setMessage('Nothing is left of the photo: fewer clicks on it, or a lower tolerance.')
    setDraft((d) => ({ part: { ...d.part, size_mm: { w: traced.w_mm, h: traced.h_mm } }, body: { kind: 'traced', svg: traced.svg } }))
    setPhoto(undefined)
    setMessage(`Traced: ${traced.w_mm} × ${traced.h_mm} mm.`)
  }
  const save = async (where: 'project' | 'library') => {
    const files = partFiles(draft)
    state.keepPart(files.stored)
    if (where === 'project') return setMessage(`${part.name} is in the project's palette.`)
    if (desktop) {
      const folder = await desktop.saveLibraryPart({ id: files.id, json: files.json, svg: files.svg })
      setMessage(folder ? `Saved in ${folder}.` : 'The library refused it.')
    } else {
      download('part.json', files.json, 'application/json')
      download('part.svg', files.svg, 'image/svg+xml')
      setMessage(`Downloaded part.json and part.svg: put them in _SOFTWARE/library/parts/${files.id}/.`)
    }
  }

  const nets = part.nets ?? []
  const setNet = (i: number, fn: (n: PartNet) => PartNet) => setPart((p) => ({ ...p, nets: (p.nets ?? []).map((n, j) => (j === i ? fn(n) : n)) }))
  const elements = part.power?.elements ?? []
  const setElements = (fn: (e: readonly PowerElement[]) => readonly PowerElement[]) => setPart((p) => ({ ...p, power: { ...p.power, elements: fn(p.power?.elements ?? []) } }))
  const chips = part.chips ?? []
  const setChips = (fn: (c: readonly ChipPlacement[]) => readonly ChipPlacement[]) => setPart((p) => ({ ...p, chips: fn(p.chips ?? []) }))
  const netChoices = nets.map((n) => ({ value: n.id, label: n.label }))
  const deviceTypes = [...catalog.devices.values()].map((t) => ({ value: t.id, label: t.label, ...(t.category ? { group: t.category } : {}) }))
  const chipTypes = [...[...state.library.values()].filter((p) => p.category === 'chip').map((p) => ({ value: p.id, label: p.name, group: 'Library' })), ...deviceTypes.map((d) => ({ ...d, group: 'Device type' }))]
  const isBoard = part.category === 'board'
  const cm = rulerCm(v.zoom)

  return (
    <div className="hw-creator">
      <div className="hw-creator-form">
        <PanelHeader title="Part creator">
          <button aria-label="Close the creator" title="Back to the canvas" onClick={state.closeCreator}><X aria-hidden="true" /></button>
        </PanelHeader>
        <CardStack>
          <Card title="Part">
            <FormRow label="Name"><TextField value={part.name} onChange={(e) => { const name = e.target.value; setPart((p) => ({ ...p, name, ...(idTouched ? {} : { id: slug(name) }) })) }} /></FormRow>
            <FormRow label="Id" hint="The folder in the library: a-z, 0-9, _"><TextField value={part.id} onChange={(e) => { setIdTouched(true); setPart((p) => ({ ...p, id: e.target.value })) }} /></FormRow>
            <FormRow label="Kind">
              <ChoiceField<PartCategory> value={part.category} choices={(Object.keys(CATEGORY_LABELS) as PartCategory[]).map((c) => ({ value: c, label: CATEGORY_LABELS[c] }))} onChange={(c) => c && setPart((p) => ({ ...p, category: c }))} />
            </FormRow>
            <FormRow label="Description"><TextField value={part.description ?? ''} onChange={(e) => setPart((p) => ({ ...p, description: e.target.value }))} /></FormRow>
            {isBoard && (
              <FormGrid>
                <FormRow label="Draws the board" hint="board.json's name"><TextField value={part.board?.name ?? board.name} onChange={(e) => setPart((p) => ({ ...p, board: { name: e.target.value, revision: p.board?.revision ?? board.revision } }))} /></FormRow>
                <FormRow label="Revision"><NumberField value={part.board?.revision ?? board.revision} integer min={0} onCommit={(r) => setPart((p) => ({ ...p, board: { name: p.board?.name ?? board.name, revision: r } }))} /></FormRow>
              </FormGrid>
            )}
          </Card>

          {!isBoard && (
            <Card title="Its device" help="The device the part is on the board: placing it adds this device to the project, with the preset's settings.">
              <FormRow label="Device type">
                <ChoiceField<string> value={part.device?.type} emptyLabel="None (a passive part)" choices={deviceTypes} onChange={(t) => setPart((p) => { const { device: _d, ...rest } = p; return t ? { ...rest, device: { type: t } } : rest })} />
              </FormRow>
              {type?.presets?.length ? (
                <FormRow label="Preset">
                  <ChoiceField<string> value={part.device?.preset} emptyLabel="None" choices={type.presets.map((pr) => ({ value: pr.id, label: pr.label, ...(pr.description ? { description: pr.description } : {}) }))}
                    onChange={(pr) => setPart((p) => ({ ...p, device: { type: p.device!.type, ...(pr ? { preset: pr } : {}) } }))} />
                </FormRow>
              ) : null}
            </Card>
          )}

          <Card title="Body" help="A plain shape, or a photo traced into a few colours. The size is the real one, in millimetres.">
            <FormGrid>
              <FormRow label="Width (mm)"><NumberField value={part.size_mm.w} min={1} max={500} onCommit={(w) => setPart((p) => ({ ...p, size_mm: { ...p.size_mm, w } }))} /></FormRow>
              <FormRow label="Height (mm)"><NumberField value={part.size_mm.h} min={1} max={500} onCommit={(h) => setPart((p) => ({ ...p, size_mm: { ...p.size_mm, h } }))} /></FormRow>
            </FormGrid>
            {draft.body.kind === 'shape' && !photo && (
              <FormGrid>
                <FormRow label="Colour"><TextField type="color" value={draft.body.colour} onChange={(e) => { const colour = e.target.value; setDraft((d) => (d.body.kind === 'shape' ? { ...d, body: { ...d.body, colour } } : d)) }} /></FormRow>
                <FormRow label="Corners (mm)"><NumberField value={draft.body.radius} min={0} max={20} onCommit={(radius) => setDraft((d) => (d.body.kind === 'shape' ? { ...d, body: { ...d.body, radius } } : d))} /></FormRow>
              </FormGrid>
            )}
            <FormRow label="Photo" hint="A picture of the part from above: then click its background on the stage">
              <TextField type="file" accept="image/*" onChange={(e) => {
                const file = e.target.files?.[0]
                if (file) void readImage(file).then((pixels) => setPhoto({ pixels, seeds: [{ x: 0, y: 0 }, { x: pixels.width - 1, y: 0 }, { x: 0, y: pixels.height - 1 }, { x: pixels.width - 1, y: pixels.height - 1 }], tolerance: 40, colours: 3, detail: 2, width_mm: part.size_mm.w }))
              }} />
            </FormRow>
            {photo && (
              <>
                <Slider label="Background tolerance" value={photo.tolerance} min={5} max={140} step={5} onChange={(tolerance) => setPhoto({ ...photo, tolerance })} />
                <Slider label="Colours" value={photo.colours} min={1} max={6} step={1} onChange={(colours) => setPhoto({ ...photo, colours })} />
                <Slider label="Detail" value={photo.detail} min={1} max={3} step={1} onChange={(detail) => setPhoto({ ...photo, detail })} />
                <FormRow label="Real width of the part (mm)"><NumberField value={photo.width_mm} min={1} max={500} onCommit={(width_mm) => setPhoto({ ...photo, width_mm })} /></FormRow>
                <div className="hw-creator-actions">
                  <Button variant="primary" onClick={trace}>Trace into a drawing</Button>
                  <Button onClick={() => setPhoto({ ...photo, seeds: photo.seeds.slice(0, 4) })}>Undo the clicks</Button>
                </div>
              </>
            )}
          </Card>

          <Card title="Power nets" help="sink: the part takes power here (what it wants and draws); source: it gives power; through: it only passes power between its pins (a breakout's V+).">
            {nets.map((n, i) => (
              <div key={i} className="hw-creator-net">
                <FormGrid>
                  <FormRow label="Name"><TextField value={n.label} onChange={(e) => { const label = e.target.value; setNet(i, (x) => ({ ...x, label, id: slug(label) })) }} /></FormRow>
                  <FormRow label="Role"><ChoiceField<NetRole> value={n.role} choices={ROLES.map((r) => ({ value: r, label: r }))} onChange={(role) => role && setNet(i, (x) => ({ ...x, role }))} /></FormRow>
                  <FormRow label="Min mV"><NumberField value={n.voltage_mV?.min} integer onCommit={(min) => setNet(i, (x) => ({ ...x, voltage_mV: { ...x.voltage_mV, min } }))} /></FormRow>
                  <FormRow label="Max mV"><NumberField value={n.voltage_mV?.max} integer onCommit={(max) => setNet(i, (x) => ({ ...x, voltage_mV: { ...x.voltage_mV, max } }))} /></FormRow>
                  {n.role === 'sink' && <FormRow label="Best mV"><NumberField value={n.voltage_mV?.nominal} integer onCommit={(nominal) => setNet(i, (x) => ({ ...x, voltage_mV: { ...x.voltage_mV, nominal } }))} /></FormRow>}
                  {n.role === 'sink' && <FormRow label="Draws mA"><NumberField value={n.current_mA?.typical} integer onCommit={(typical) => setNet(i, (x) => ({ ...x, current_mA: { ...x.current_mA, typical } }))} /></FormRow>}
                  {n.role === 'sink' && <FormRow label="Peak mA"><NumberField value={n.current_mA?.peak} integer onCommit={(peak) => setNet(i, (x) => ({ ...x, current_mA: { ...x.current_mA, peak } }))} /></FormRow>}
                  {n.role !== 'sink' && <FormRow label="Carries mA"><NumberField value={n.max_mA} integer onCommit={(max_mA) => setNet(i, (x) => ({ ...x, max_mA }))} /></FormRow>}
                </FormGrid>
                <Button size="sm" variant="danger" onClick={() => setPart((p) => ({ ...p, nets: (p.nets ?? []).filter((_, j) => j !== i) }))}>Remove {n.label}</Button>
              </div>
            ))}
            <Button variant="dashed" block onClick={() => setPart((p) => ({ ...p, nets: [...(p.nets ?? []), { id: `vcc${(p.nets?.length ?? 0) || ''}`, label: `VCC${(p.nets?.length ?? 0) || ''}`, role: 'sink', voltage_mV: { min: 4500, max: 5500, nominal: 5000 }, current_mA: { typical: 100, peak: 200 } }] }))}>Add a power net</Button>
          </Card>

          <Card title="Connectors" help="Standard parts at their true size: drag one on the stage (it snaps to a quarter of 2.54 mm; Alt: freely).">
            <div className="hw-creator-actions">
              {CONNECTOR_TYPES.map((t) => <Button key={t} size="sm" onClick={() => addConnector(t)}>{CONNECTOR_LABELS[t]}</Button>)}
            </div>
            {part.connectors.length > 0 && (
              <ChoiceField<string> value={selected} emptyLabel="Pick a connector" choices={part.connectors.map((c) => ({ value: c.id, label: c.label ?? c.id }))} onChange={setSelected} />
            )}
            {connector && (
              <>
                <FormGrid>
                  <FormRow label="Label"><TextField value={connector.label ?? ''} onChange={(e) => { const label = e.target.value; setConnector(connector.id, (c) => ({ ...c, label })) }} /></FormRow>
                  <FormRow label="Pins"><NumberField value={connector.pins.length} integer min={1} max={80} onCommit={(n) => setConnector(connector.id, (c) => withPinCount(c, n))} /></FormRow>
                  {(connector.type === 'goldpin' || connector.type === 'goldpin_female') && <FormRow label="Rows"><NumberField value={connector.rows ?? 1} integer min={1} max={4} onCommit={(rows) => setConnector(connector.id, (c) => ({ ...c, rows }))} /></FormRow>}
                  {connector.type === 'screw_terminal' && (
                    <FormRow label="Pitch"><ChoiceField<number> value={connector.pitch_mm ?? 5} choices={SCREW_PITCHES.map((p) => ({ value: p, label: `${p} mm` }))} onChange={(pitch_mm) => pitch_mm && setConnector(connector.id, (c) => ({ ...c, pitch_mm }))} /></FormRow>
                  )}
                  <FormRow label="X (mm)"><NumberField value={connector.at.x} onCommit={(x) => setConnector(connector.id, (c) => ({ ...c, at: { ...c.at, x } }))} /></FormRow>
                  <FormRow label="Y (mm)"><NumberField value={connector.at.y} onCommit={(y) => setConnector(connector.id, (c) => ({ ...c, at: { ...c.at, y } }))} /></FormRow>
                </FormGrid>
                {connector.type !== 'pad' && (
                  <SegmentedSwitch label="Turned" value={String(connector.rotation ?? 0) as '0'} options={ROTATIONS} onChange={(r) => setConnector(connector.id, (c) => ({ ...c, rotation: Number(r) as Rotation }))} />
                )}
                <div className="hw-pin-rows">
                  {connector.pins.map((pin, i) => (
                    <div key={i} className="hw-pin-row">
                      <span className="hw-pin-index">{i + 1}</span>
                      <TextField value={pin.label} aria-label={`Pin ${i + 1} label`} onChange={(e) => { const label = e.target.value; setConnector(connector.id, (c) => ({ ...c, pins: c.pins.map((p, j) => (j === i ? { ...p, label } : p)) })) }} />
                      <ChoiceField<string> value={roleOf(pin)} choices={roles} onChange={(role) => setConnector(connector.id, (c) => ({ ...c, pins: c.pins.map((p, j) => (j === i ? withRole(p, role ?? 'nc') : p)) }))} />
                    </div>
                  ))}
                </div>
                <Button variant="danger" size="sm" onClick={() => { setPart((p) => ({ ...p, connectors: p.connectors.filter((c) => c.id !== connector.id) })); setSelected(undefined) }}>
                  <Trash2 size={13} aria-hidden="true" /> Remove the connector
                </Button>
              </>
            )}
          </Card>

          {(isBoard || part.category === 'module' || part.category === 'power') && (
            <Card title="Flow of energy" help="How power goes inside: a source, an eFuse, a regulator (adjustable: the canvas sets it), a mux, the part's own load; each between its nets.">
              {elements.map((el, i) => {
                const set = (fn: (e: PowerElement) => PowerElement) => setElements((list) => list.map((x, j) => (j === i ? fn(x) : x)))
                const ins = el.in === undefined ? [] : typeof el.in === 'string' ? [el.in] : el.in
                return (
                  <div key={i} className="hw-creator-net">
                    <FormGrid>
                      <FormRow label="Kind"><ChoiceField<ElementKind> value={el.kind} choices={ELEMENT_KINDS.map((k) => ({ value: k, label: k }))} onChange={(kind) => kind && set((x) => ({ ...x, kind, ...(kind === 'mux' ? { in: [ins[0] ?? '', ins[1] ?? ''] } : {}) }))} /></FormRow>
                      <FormRow label="Name"><TextField value={el.label} onChange={(e) => { const label = e.target.value; set((x) => ({ ...x, label })) }} /></FormRow>
                      {el.kind !== 'source' && (el.kind === 'mux' ? [0, 1] : [0]).map((k) => (
                        <FormRow key={k} label={el.kind === 'mux' ? `Input ${k === 0 ? 'A' : 'B'}` : 'From'}>
                          <ChoiceField<string> value={ins[k]} choices={netChoices} onChange={(n) => set((x) => ({ ...x, in: el.kind === 'mux' ? [k === 0 ? n ?? '' : ins[0] ?? '', k === 1 ? n ?? '' : ins[1] ?? ''] : n ?? '' }))} />
                        </FormRow>
                      ))}
                      {el.kind !== 'load' && <FormRow label="To"><ChoiceField<string> value={el.out} choices={netChoices} onChange={(out) => set((x) => ({ ...x, ...(out ? { out } : {}) }))} /></FormRow>}
                      <FormRow label="Board device"><ChoiceField<number> value={el.device} emptyLabel="None" choices={boardDevices.map((d) => ({ value: d.id, label: d.name }))} onChange={(device) => set((x) => { const { device: _d, ...rest } = x; return device === undefined ? rest : { ...rest, device } })} /></FormRow>
                      {(el.kind === 'source' || el.kind === 'regulator') && <FormRow label="Min mV"><NumberField value={el.voltage_mV?.min} integer onCommit={(min) => set((x) => ({ ...x, voltage_mV: { ...x.voltage_mV, min } }))} /></FormRow>}
                      {(el.kind === 'source' || el.kind === 'regulator') && <FormRow label="Max mV"><NumberField value={el.voltage_mV?.max} integer onCommit={(max) => set((x) => ({ ...x, voltage_mV: { ...x.voltage_mV, max } }))} /></FormRow>}
                      <FormRow label={el.kind === 'load' ? 'Draws mA' : 'Limit mA'}><NumberField value={el.kind === 'load' ? el.current_mA?.typical : el.max_mA} integer onCommit={(n) => set((x) => (x.kind === 'load' ? { ...x, current_mA: { typical: n } } : { ...x, max_mA: n }))} /></FormRow>
                    </FormGrid>
                    {el.kind === 'regulator' && <Switch label="Adjustable (the canvas sets it)" checked={!!el.adjustable} onChange={(adjustable) => set((x) => ({ ...x, adjustable }))} />}
                    <Button size="sm" variant="danger" onClick={() => setElements((list) => list.filter((_, j) => j !== i))}>Remove {el.label}</Button>
                  </div>
                )
              })}
              <Button variant="dashed" block onClick={() => setElements((list) => [...list, { id: `el${list.length + 1}`, kind: 'regulator', label: `Regulator ${list.length + 1}`, in: nets[0]?.id ?? '', out: nets[1]?.id ?? '' }])}>Add an element</Button>
            </Card>
          )}

          {isBoard && (
            <Card title="Chips" help="The chips on the board, drawn as blocks (or a library part's own drawing: the ESP32-S3 module).">
              {chips.map((chip, i) => {
                const set = (fn: (c: ChipPlacement) => ChipPlacement) => setChips((list) => list.map((x, j) => (j === i ? fn(x) : x)))
                return (
                  <div key={chip.id} className="hw-creator-net">
                    <FormGrid>
                      <FormRow label="Label"><TextField value={chip.label} onChange={(e) => { const label = e.target.value; set((x) => ({ ...x, label })) }} /></FormRow>
                      <FormRow label="Is"><ChoiceField<string> value={chip.type} emptyLabel="A generic chip" choices={chipTypes} onChange={(t) => set((x) => { const { type: _t, ...rest } = x; return t ? { ...rest, type: t } : rest })} /></FormRow>
                      <FormRow label="Board device"><ChoiceField<number> value={chip.device} emptyLabel="None" choices={boardDevices.map((d) => ({ value: d.id, label: d.name }))} onChange={(device) => set((x) => { const { device: _d, ...rest } = x; return device === undefined ? rest : { ...rest, device } })} /></FormRow>
                      <FormRow label="X (mm)"><NumberField value={chip.at.x} onCommit={(x0) => set((x) => ({ ...x, at: { ...x.at, x: x0 } }))} /></FormRow>
                      <FormRow label="Y (mm)"><NumberField value={chip.at.y} onCommit={(y0) => set((x) => ({ ...x, at: { ...x.at, y: y0 } }))} /></FormRow>
                      <FormRow label="W × H (mm)"><NumberField value={chip.size_mm.w} min={1} onCommit={(w) => set((x) => ({ ...x, size_mm: { ...x.size_mm, w } }))} /></FormRow>
                      <FormRow label=" "><NumberField value={chip.size_mm.h} min={1} onCommit={(h) => set((x) => ({ ...x, size_mm: { ...x.size_mm, h } }))} /></FormRow>
                    </FormGrid>
                    <Button size="sm" variant="danger" onClick={() => setChips((list) => list.filter((_, j) => j !== i))}>Remove {chip.label}</Button>
                  </div>
                )
              })}
              <div className="hw-creator-actions">
                <Button size="sm" onClick={() => setChips((list) => [...list, { id: `chip${list.length + 1}`, label: `U${list.length + 1}`, at: { x: 5, y: 5 }, size_mm: { w: 6, h: 6 } }])}>Generic chip</Button>
                {state.library.get('esp32s3_wroom1') && (
                  <Button size="sm" onClick={() => {
                    const esp = state.library.get('esp32s3_wroom1')!
                    setChips((list) => [...list, { id: `esp${list.length + 1}`, label: 'ESP32-S3', type: esp.id, device: 0, at: { x: 5, y: 5 }, size_mm: esp.size_mm }])
                  }}>ESP32-S3 module</Button>
                )}
              </div>
            </Card>
          )}

          <Card title="Save">
            {problems.map((p) => <FieldError key={p}>{p}</FieldError>)}
            <div className="hw-creator-actions">
              <Button variant="primary" disabled={problems.length > 0} onClick={() => void save('project')}>Use in this project</Button>
              <Button disabled={problems.length > 0} onClick={() => void save('library')}>Save to the library</Button>
              <Button onClick={state.closeCreator}>Close</Button>
            </div>
            {message && <p>{message}</p>}
          </Card>
        </CardStack>
      </div>

      <div ref={stage} className="hw-creator-stage hw-canvas" onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onWheel={onWheel}>
        <svg className="hw-svg" width="100%" height="100%">
          <defs>
            <pattern id="hw-mm" width={2.54 * v.zoom} height={2.54 * v.zoom} patternUnits="userSpaceOnUse" x={v.x} y={v.y}>
              <circle cx={0} cy={0} r={0.9} className="hw-grid-dot" fill="currentColor" opacity={0.25} />
            </pattern>
          </defs>
          <rect width="100%" height="100%" fill="url(#hw-mm)" />
          <g transform={`translate(${v.x} ${v.y}) scale(${v.zoom})`}>
            {photo && photoUrl && box && (
              <image data-photo="1" className="hw-photo" href={photoUrl} x={-box.x0 * mmPerPx} y={-box.y0 * mmPerPx} width={photo.pixels.width * mmPerPx} height={photo.pixels.height * mmPerPx} preserveAspectRatio="none" />
            )}
            {!photo && <image href={svgUrl(svg)} x={0} y={0} width={part.size_mm.w} height={part.size_mm.h} preserveAspectRatio="none" />}
            <rect x={0} y={0} width={part.size_mm.w} height={part.size_mm.h} fill="none" stroke="currentColor" strokeOpacity={0.35} strokeWidth={0.15} strokeDasharray="1 0.6" pointerEvents="none" />
            {chips.map((chip) => {
              const lib = chip.type ? state.library.get(chip.type) : undefined
              return lib ? <image key={chip.id} href={svgUrl(lib.svg)} x={chip.at.x} y={chip.at.y} width={chip.size_mm.w} height={chip.size_mm.h} />
                : <g key={chip.id} className="hw-chip"><rect x={chip.at.x} y={chip.at.y} width={chip.size_mm.w} height={chip.size_mm.h} rx={0.4} /><text x={chip.at.x + chip.size_mm.w / 2} y={chip.at.y + chip.size_mm.h / 2} fontSize={Math.min(2.2, chip.size_mm.h / 3)}>{chip.label}</text></g>
            })}
            {part.connectors.map((c) => {
              const body = connectorBody(c)
              return (
                <g key={c.id}>
                  <ConnectorShape c={c} />
                  <rect className={`hw-connector-hit ${selected === c.id ? 'is-selected' : ''}`.trim()} data-connector={c.id} x={c.at.x + body.x0} y={c.at.y + body.y0} width={body.x1 - body.x0} height={body.y1 - body.y0} transform={`rotate(${c.rotation ?? 0} ${c.at.x} ${c.at.y})`} />
                  {connectorPinPoints(c).map((p, i) => <text key={i} x={p.x} y={p.y - 1.5} fontSize={0.9} textAnchor="middle" fill="currentColor" pointerEvents="none">{c.pins[i]?.label}</text>)}
                </g>
              )
            })}
          </g>
        </svg>
        <div className="hw-ruler"><div className="hw-ruler-bar" style={{ width: cm * 10 * v.zoom }} /><span>{cm < 1 ? `${cm * 10} mm` : `${cm} cm`}</span></div>
        {photo && <div className="hw-hint is-top">Click the background of the photo to take it away · then Trace</div>}
        <div className="hw-tools" onPointerDown={(e) => e.stopPropagation()}>
          {draft.body.kind === 'keep' && <Badge tone="info">editing</Badge>}
          <button aria-label="Zoom out" onClick={() => setView(zoomAt(v, 1 / 1.25, { x: 200, y: 200 }))}><Minus aria-hidden="true" /></button>
          <span className="hw-zoom-value">{Math.round(v.zoom * 10) / 10} px/mm</span>
          <button aria-label="Zoom in" onClick={() => setView(zoomAt(v, 1.25, { x: 200, y: 200 }))}><Plus aria-hidden="true" /></button>
          <button aria-label="Fit" title="Show the whole part" onClick={fit}><Maximize aria-hidden="true" /></button>
        </div>
      </div>
    </div>
  )
}
