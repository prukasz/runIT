# Design Step 01: TopBar Layout

## Implemented Component
- File: `app/src/components/TopBar.tsx`
- Parent Frame: `app/src/App.tsx`

## Structure & Icons
- **Left Group**:
  - `Settings` icon (`lucide-react/Settings`)
  - Separator
  - `Board` icon (`lucide-react/Cpu`)
  - `Devices` icon (`lucide-react/Boxes`)
  - `Code` icon (`lucide-react/Code2`)
  - `Remote` icon (`lucide-react/Gamepad2`)
- **Right Group**:
  - `Run` icon (`lucide-react/Play`)
  - `Debug` icon (`lucide-react/Bug`)
  - Separator
  - `Connect Bluetooth` icon (`lucide-react/Bluetooth`)
  - `Connect WiFi` icon (`lucide-react/Wifi`)

## Styling
- Dark mode theme: Background `#18181b`, border `#27272a`, icon base color `text-zinc-300`, hover states with clean subtle feedback.
- Height: Standard compact header `h-12`.

