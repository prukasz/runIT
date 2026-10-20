// The page's side of the board chooser (main.mjs): `window.runitDesktop`, read by src/app/desktop.ts. A sandboxed preload is CommonJS.
const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('runitDesktop', {
  want: (id) => ipcRenderer.send('ble:want', id ?? null),
  select: (id) => ipcRenderer.send('ble:select', id ?? ''),
  gesture: () => ipcRenderer.invoke('ble:gesture'),
  getZoom: () => ipcRenderer.invoke('zoom:get'),
  setZoom: (factor) => ipcRenderer.send('zoom:set', factor),
  onZoom: (handler) => {
    const listener = (_event, factor) => handler(factor)
    ipcRenderer.on('zoom:changed', listener)
    return () => ipcRenderer.removeListener('zoom:changed', listener)
  },
  onDevices: (handler) => {
    const listener = (_event, devices) => handler(devices)
    ipcRenderer.on('ble:devices', listener)
    return () => ipcRenderer.removeListener('ble:devices', listener)
  },
})
