import { Cpu, FolderOpen, HardDriveDownload, HardDriveUpload, Save } from 'lucide-react'
import { Badge, Button, Card, CardStack, FieldError, FormGrid, FormRow, PanelHeader, TreeSlab } from '@ui/components'
import type { BoardProfileState } from './useBoardProfile'
import type { DevicesState } from './useDevices'

/*
 * The page of the Project view's Board profile tab while no device of it is chosen: the board's profile as one file
 * (`.runit-board.json`, a project file whose devices are the board's: devices/boardEditor.ts), with its devices and their Board
 * boot, its I2C buses and UART ports. Presets load the bundled runIT PCB (rev 1, all 15 chips) or ESP32-S3 DevKit directly; Open
 * replaces from a file; Write stores it on the board and restarts. A device in its list opens that device's page in the tab.
 */
export function BoardProfileScreen({ profile, editor }: { profile: BoardProfileState; editor?: DevicesState }) {
  const p = profile.project
  const boot = p.devices.reduce((n, d) => n + (d.defaults?.length ?? 0), 0)
  const rails = p.power?.rails ?? []
  const connected = editor?.connected ?? false
  const busy = Boolean(editor?.busy)

  return (
    <div className="project-screen">
      <PanelHeader title="Board profile">
        <Button size="sm" title="Load the bundled runIT PCB rev 1 profile (all 15 onboard chips & power layout)" onClick={() => profile.loadPreset('pcb')}>
          <Cpu aria-hidden="true" />runIT PCB (r1)
        </Button>
        <Button size="sm" title="Load the bundled ESP32-S3 DevKit profile (GPIO + 2 I2C buses)" onClick={() => profile.loadPreset('devkit')}>
          DevKit
        </Button>
        <Button size="sm" title="Open a board profile file (runit-app/profiles has the bundled boards'); Undo goes back" onClick={profile.open}>
          <FolderOpen aria-hidden="true" />Open…
        </Button>
        <Button size="sm" title="Save this board profile as a file" onClick={profile.save}>
          <Save aria-hidden="true" />Save
        </Button>
        {editor && (
          <>
            <Button size="sm" disabled={!connected || busy} title="Read the profile currently stored on the board" onClick={editor.read}>
              <HardDriveDownload aria-hidden="true" />Read
            </Button>
            <Button size="sm" variant="primary" disabled={!connected || busy} title="Write this profile to the board and restart it so it creates its devices" onClick={editor.write}>
              <HardDriveUpload aria-hidden="true" />Write
            </Button>
          </>
        )}
      </PanelHeader>
      <p className="project-note">What the board is: its devices with their settings, power layout and Board boot (I2C buses and UART ports are devices too). On connect the board's profile is loaded here; a board that keeps none receives this one and restarts. Write puts your changes on the board.</p>
      {profile.note && (profile.note.tone === 'error'
        ? <FieldError>{profile.note.text}</FieldError>
        : <p className="project-note" role="status" onClick={profile.clearNote}>{profile.note.text}</p>)}
      {editor?.note && (editor.note.tone === 'error'
        ? <FieldError>{editor.note.text}</FieldError>
        : <p className="project-note" role="status" onClick={editor.clearNote}>{editor.note.text}</p>)}
      <CardStack>
        <Card title={p.name} subhead="The profile in the app now">
          <FormGrid>
            <FormRow label="Devices"><span>{p.devices.length}</span></FormRow>
            <FormRow label="Board boot commands"><span>{boot}</span></FormRow>
            <FormRow label="Power rails"><span>{rails.length > 0 ? rails.map((r) => r.name).join(', ') : 'None'}</span></FormRow>
          </FormGrid>
        </Card>
        <Card title={`Onboard devices (${profile.board.devices.length})`} subhead="Created in dependency order at boot">
          {profile.board.devices.map((d) => {
            const live = editor?.live.get(d.id)
            const addr = d.i2c ? `I2C #${d.i2c.bus} · 0x${d.i2c.address.toString(16).toUpperCase().padStart(2, '0')}` : undefined
            return (
              <TreeSlab
                key={d.id}
                label={`#${d.id} · ${d.name}`}
                icon={<Cpu aria-hidden="true" />}
                {...(editor ? { onClick: () => editor.select({ kind: 'device', id: d.id }) } : {})}
              >
                <Badge tone="neutral" size="compact">{d.descriptor.replace(/^device_/, '')}</Badge>
                {addr && <Badge tone="neutral" size="compact">{addr}</Badge>}
                {live?.state === 'present' && <Badge tone="success" size="compact">present</Badge>}
                {live?.state === 'missing' && <Badge tone="warning" size="compact">missing</Badge>}
              </TreeSlab>
            )
          })}
        </Card>
      </CardStack>
    </div>
  )
}
