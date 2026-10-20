import { Card, CardStack, FormRow, PanelHeader, Slider, Switch } from '@ui/components'
import { useAppPrefs } from './AppPrefs'
import type { useAppZoom } from './useAppZoom'

/*
 * The Project view's App settings tab: what belongs to the app on this computer, not to the project or the board.
 */

const percent = (factor: number) => `${Math.round(factor * 100)} %`

export function AppSettingsScreen({ zoom }: { zoom: ReturnType<typeof useAppZoom> }) {
  const prefs = useAppPrefs()
  return (
    <div className="project-screen">
      <PanelHeader title="App settings" />
      <CardStack>
        <Card title="Interface scale" help="How large the app draws everything. Ctrl + / Ctrl − change it too, Ctrl 0 returns to 130 %.">
          {zoom.zoom === undefined
            ? <p className="project-note">The desktop app sets its scale here; in a browser, use the browser's zoom (Ctrl + / Ctrl −).</p>
            : (
              <FormRow label="Scale">
                <Slider label="Interface scale" min={0.7} max={2} step={0.1} value={zoom.zoom} format={percent} onChange={zoom.setZoom} />
              </FormRow>
            )}
        </Card>
        <Card title="Controls" help="A field the descriptor marks (a drive, a duty, a current limit; an on / off choice) can be drawn as a slider or a switch. Fields it does not mark stay as they are.">
          <FormRow label="Sliders for values" note="A number with a range is a slider instead of a number field.">
            <Switch label={prefs.sliders ? 'Allowed' : 'Not allowed'} checked={prefs.sliders} onChange={prefs.setSliders} />
          </FormRow>
          <FormRow label="Switches for on / off" note="A choice of two (Off / On, No / Yes, Low / High) is a switch instead of a list.">
            <Switch label={prefs.switches ? 'Allowed' : 'Not allowed'} checked={prefs.switches} onChange={prefs.setSwitches} />
          </FormRow>
        </Card>
      </CardStack>
    </div>
  )
}
