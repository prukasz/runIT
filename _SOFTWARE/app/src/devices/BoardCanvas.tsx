import { Cpu } from 'lucide-react'
import { PanelHeader } from '../components/PanelHeader/PanelHeader'
import './Devices.css'

/*
 * The Board view's canvas (View / Manage's counterpart): the board itself, with the devices attached to it drawn and wired
 * on it. Not built yet; the main screen is this placeholder until the board profile (terminals, connectors, which device
 * channel sits where) is published (features.md F-GAP-3).
 */
export function BoardCanvas() {
  return (
    <div className="object-editor board-canvas">
      <PanelHeader icon={<Cpu aria-hidden="true" />} title="Board canvas" />
      <p className="devices-muted">The board and the devices attached to it will be drawn here. Use Switch to View / Manage (bottom left) for the device pages.</p>
    </div>
  )
}
