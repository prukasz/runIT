import { useState } from 'react'
import { Crosshair, RotateCw, Trash2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { Button, ToggleChip } from '../components/Button'
import { Card } from '../components/Card'
import { ChoiceField } from '../components/ChoiceField'
import { FormGrid, FormRow, TextField } from '../components/FormField'
import { PaletteSearch } from '../components/PaletteSearch'
import { PanelHeader } from '../components/PanelHeader/PanelHeader'
import { TreeSlab } from '../components/TreeSlab'
import type { DevicePort } from '../domain/descriptors'
import type { Anchor } from '../domain/devices/componentLayout'
import { flowOf, FLOW_LABEL } from '../domain/hardware'
import { FlowBadge } from './BoardWiringDetails'
import type { ComponentEditorApi } from './useComponentEditor'
import './Devices.css'

const MODES = ['BINARY', 'PWM', 'ADC', 'DAC'] as const
const DIRS = ['in', 'out', 'inout'] as const
/** A pin's role in words: the csv `dir` says it (in = consumer, out = producer, inout = either). */
const roleOf = (dir: (typeof DIRS)[number], kind: Parameters<typeof flowOf>[0]['kind']): string => FLOW_LABEL[flowOf({ kind, dir })]

const numberOrUndefined = (text: string): number | undefined => {
  const value = Number(text)
  return text.trim() === '' || !Number.isInteger(value) || value < 0 ? undefined : value
}

const matches = (port: DevicePort, query: string): boolean => {
  const q = query.trim().toLowerCase()
  return !q || [port.name, port.kind, port.group, port.note].some((text) => text?.toLowerCase().includes(q))
}

/**
 * The anchor editor's right panel: the device's pins (from its pins csv) with what is placed, and for the selected pin
 * the limits this variant gives it. Select a pin, then click the picture to place it. A pin left unplaced is not
 * brought out by the variant.
 */
export function ComponentPins({ editor: e }: { editor: ComponentEditorApi }) {
  const [query, setQuery] = useState('')
  const sel = e.selected ? e.ports.find((port) => port.name === e.selected) : undefined
  const selAnchor = e.selected ? e.anchors[e.selected] : undefined
  const shown = e.ports.filter((port) => matches(port, query))

  return (
    <div className="program-panel devices-details component-pins">
      <Card title="Variant" className="component-variant-card">
        <FormRow label="Name" htmlFor="variant-name" note="The variant's ID and folder name (device_x/app/canvas/<name>/): lower case letters, digits and _, starting with a letter. Changing the name of a saved variant renames its files when you Save." hint={e.nameTaken ? `A variant named ${e.nameDraft.trim().toLowerCase()} already exists.` : e.isRename ? `Saving renames ${e.variant} to ${e.nameDraft.trim().toLowerCase()}.` : undefined}>
          <TextField id="variant-name" className="form-field-panel" value={e.nameDraft} placeholder="e.g. breakout_v2" aria-invalid={(!!e.nameDraft && !e.nameOk) || e.nameTaken} onChange={(event) => e.setNameDraft(event.target.value)} />
        </FormRow>
        <FormRow label="Label" htmlFor="variant-label" note="The name shown in the variants list. The name is used when this is empty.">
          <TextField id="variant-label" className="form-field-panel" value={e.variantLabel} placeholder={e.nameDraft.trim() || 'Shown in the variants list'} onChange={(event) => e.setVariantLabel(event.target.value)} />
        </FormRow>
      </Card>

      <PanelHeader title="Pins"><Badge tone="count" push>{e.placed} / {e.ports.length} placed</Badge></PanelHeader>
      <p className="devices-muted component-pins-hint">{e.picture ? 'Select a pin, then click the picture to place it. Unplaced pins are not brought out.' : 'Choose a picture in the toolbar first.'}</p>

      {sel && (
        <Card title={sel.name} subhead={sel.note} className="component-pin-card">
          {selAnchor ? (
            <>
              <div className="component-pin-actions">
                <Button size="sm" onClick={() => e.place(sel.name, 0.5, 0.5)}><Crosshair aria-hidden="true" />Centre</Button>
                <Button size="sm" title="Turn this pin's name a quarter turn (R)" onClick={() => e.rotateLabel(sel.name)}><RotateCw aria-hidden="true" />Name {e.angleOf(sel.name)}°</Button>
                <Button size="sm" variant="danger" onClick={() => e.unplace(sel.name)}><Trash2 aria-hidden="true" />Not brought out</Button>
              </div>
              <FormGrid>
                <FormRow label="mV min" htmlFor="pin-mv-min">
                  <TextField id="pin-mv-min" inputMode="numeric" className="form-field-panel" value={selAnchor.mV_min ?? ''} placeholder={sel.mV_min === undefined ? '—' : String(sel.mV_min)} onChange={(event) => e.update(sel.name, { mV_min: numberOrUndefined(event.target.value) })} />
                </FormRow>
                <FormRow label="mV max" htmlFor="pin-mv-max">
                  <TextField id="pin-mv-max" inputMode="numeric" className="form-field-panel" value={selAnchor.mV_max ?? ''} placeholder={sel.mV_max === undefined ? '—' : String(sel.mV_max)} onChange={(event) => e.update(sel.name, { mV_max: numberOrUndefined(event.target.value) })} />
                </FormRow>
              </FormGrid>
              <FormRow label="Role" htmlFor="pin-dir" note="Producer drives the signal, consumer receives it, either can take the role the other end needs. A link needs a producer at one end and a consumer at the other. Stored as the csv direction (out, in, inout).">
                <ChoiceField<string> id="pin-dir" selectClassName="form-field-panel" value={selAnchor.dir ?? ''} onChange={(next) => next !== undefined && e.update(sel.name, { dir: (next || undefined) as Anchor['dir'] })}
                  choices={[{ value: '', label: `${roleOf(sel.dir, sel.kind)} (from the csv)` }, ...DIRS.filter((dir) => dir !== sel.dir).map((dir) => ({ value: dir as string, label: roleOf(dir, sel.kind) }))]} />
              </FormRow>
              <FormRow label="Modes" hint={selAnchor.modes ? undefined : `From the csv: ${(sel.modes ?? []).join(', ') || 'none'}`}>
                <div className="component-pin-modes">
                  {MODES.map((mode) => {
                    const current = selAnchor.modes ?? sel.modes ?? []
                    return (
                      <ToggleChip key={mode} selected={current.includes(mode)} onClick={() => {
                        const next = MODES.filter((entry) => (entry === mode ? !current.includes(mode) : current.includes(entry)))
                        e.update(sel.name, { modes: next.join('|') === (sel.modes ?? []).join('|') ? undefined : next })
                      }}>{mode}</ToggleChip>
                    )
                  })}
                </div>
              </FormRow>
            </>
          ) : (
            <p className="devices-muted">Click the picture to place {sel.name}.</p>
          )}
        </Card>
      )}

      <PaletteSearch value={query} onChange={setQuery} placeholder="Search pins..." label="Search pins" />
      <div className="component-pin-list" role="list" aria-label="Pins">
        {shown.map((port) => (
          <TreeSlab
            key={port.name}
            role="listitem"
            selected={e.selected === port.name}
            isMatch={!!query.trim()}
            onClick={() => e.setSelected(port.name)}
            icon={<span className={`component-pin-dot kind-${port.kind}`} aria-hidden="true" />}
            label={port.name}
            title={port.note}
            badges={<><Badge size="compact">{port.kind}</Badge><FlowBadge spec={{ kind: port.kind, dir: e.anchors[port.name]?.dir ?? port.dir }} />{e.anchors[port.name] ? <Badge tone="success" size="compact">placed</Badge> : e.selected === port.name && e.picture ? <Badge tone="info" size="compact">click picture</Badge> : null}</>}
          />
        ))}
        {!shown.length && <p className="devices-muted">No pin matches.</p>}
      </div>
    </div>
  )
}
