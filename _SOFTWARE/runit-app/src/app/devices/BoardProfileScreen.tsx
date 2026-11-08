import { FolderOpen, Save } from 'lucide-react'
import { Button, Card, CardStack, FieldError, FormGrid, FormRow, PanelHeader } from '@ui/components'
import type { BoardProfileState } from './useBoardProfile'

/*
 * The Project view's Board profile tab: the board's profile as one file (`.runit-board.json`, a project file whose devices are the
 * board's: devices/boardEditor.ts), with its devices and their Board boot, its I2C buses and UART ports. Open replaces the app's
 * profile with a file's (Undo goes back), so a profile is made and shared that way: `runit-app/profiles/` has the bundled boards'
 * (the runIT PCB, the devkit). Write puts it on the board.
 */
export function BoardProfileScreen({ profile }: { profile: BoardProfileState }) {
  const p = profile.project
  const boot = p.devices.reduce((n, d) => n + (d.defaults?.length ?? 0), 0)
  return (
    <div className="project-screen">
      <PanelHeader title="Board profile">
        <Button size="sm" title="Open a board profile file (runit-app/profiles has the bundled boards'); Undo goes back" onClick={profile.open}><FolderOpen aria-hidden="true" />Open…</Button>
        <Button size="sm" title="Save this board profile as a file" onClick={profile.save}><Save aria-hidden="true" />Save</Button>
      </PanelHeader>
      <p className="project-note">What the board is: its devices with their settings and Board boot, its I2C buses and UART ports. Kept in the app and on the board; Write puts it there. Open a file to start from a board's defaults: runit-app/profiles has the runIT board's and the devkit's.</p>
      {profile.note && (profile.note.tone === 'error'
        ? <FieldError>{profile.note.text}</FieldError>
        : <p className="project-note" role="status" onClick={profile.clearNote}>{profile.note.text}</p>)}
      <CardStack>
        <Card title={p.name} subhead="The profile in the app now">
          <FormGrid>
            <FormRow label="Devices"><span>{p.devices.length}</span></FormRow>
            <FormRow label="Board boot commands"><span>{boot}</span></FormRow>
            <FormRow label="I2C buses"><span>{p.i2cBuses?.length ?? 0}</span></FormRow>
            <FormRow label="UART ports"><span>{p.uartPorts?.length ?? 0}</span></FormRow>
          </FormGrid>
        </Card>
      </CardStack>
    </div>
  )
}
