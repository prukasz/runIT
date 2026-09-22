# Design Step 02: Sidebar & Main Screen Frame

## Implemented Features
- Updated `app/src/components/TopBar.tsx`:
  - Added `activeTab` state handling (`'settings' | 'board' | 'devices' | 'code' | 'remote' | null`).
  - Active button highlights with background `#27272a` and white icon.
  - Clicking the currently active icon toggles/closes the sidebar.
- Updated `app/src/App.tsx`:
  - Added collapsible and **resizable left sidebar** (min: 200px, max: 600px, default: 280px).
  - Drag handle on right edge of sidebar with subtle hover highlight and smooth `col-resize` cursor.
  - Sidebar header displaying the current tab title and a quick close (`✕`) button.
  - Full-height central workspace (`main`) that flexes to take up remaining viewport space.

