import { app, BrowserWindow, ipcMain, session } from 'electron'
import { fileURLToPath } from 'node:url'

/*
 * The desktop app: the same page as in the browser, in Electron's Chromium. What it adds is the board chooser. Electron has no
 * chooser of its own: `navigator.bluetooth.requestDevice()` fires `select-bluetooth-device` here, again with every new scan
 * result, until it is answered. The page decides: it names the board it wants (`ble:want`, picked as soon as the scan sees it)
 * or shows the scan as a list and answers with the user's choice (`ble:select`, '' cancels). `ble:gesture` lets the page call
 * `requestDevice()` with no click (a reconnect when the app starts): the call needs a user gesture, and `executeJavaScript`
 * with `userGesture` gives one.
 */

const at = (path) => fileURLToPath(new URL(path, import.meta.url))

// The link to the board runs on timers (the dead-man's heartbeat, reconnecting, command timeouts): a window in the background
// or behind another one must not have them slowed down, as Chromium does by default (to once a second, then once a minute).
app.commandLine.appendSwitch('disable-background-timer-throttling')
app.commandLine.appendSwitch('disable-renderer-backgrounding')
app.commandLine.appendSwitch('disable-backgrounding-occluded-windows')

const createWindow = () => {
  const window = new BrowserWindow({
    width: 1400,
    height: 900,
    backgroundColor: '#1f1f1f',
    autoHideMenuBar: true,
    webPreferences: { preload: at('./preload.cjs'), contextIsolation: true, sandbox: true, backgroundThrottling: false, zoomFactor: 1.3 },
  })

  // Ctrl + / Ctrl = zoom in, Ctrl - out, Ctrl 0 back to the start scale (the default menu's zoom needs Shift for the plus).
  window.webContents.on('before-input-event', (event, input) => {
    if (input.type !== 'keyDown' || !(input.control || input.meta) || input.alt) return
    const zoom = window.webContents.getZoomFactor()
    let next = null
    if (input.key === '+' || input.key === '=') next = zoom + 0.1
    else if (input.key === '-' || input.key === '_') next = zoom - 0.1
    else if (input.key === '0') next = 1.3
    if (next === null) return
    event.preventDefault()
    setZoom(next)
  })
  // The page's settings tab shows the scale and sets it too (`zoom:get`, `zoom:set`); a change made here is sent to it (`zoom:changed`).
  const setZoom = (factor) => {
    if (typeof factor !== 'number' || !Number.isFinite(factor)) return
    const next = Math.min(3, Math.max(0.5, Math.round(factor * 100) / 100))
    window.webContents.setZoomFactor(next)
    window.webContents.send('zoom:changed', next)
  }

  let wanted = null // the board to pick as soon as the scan sees it (the id the scan reports)
  let answer = null // the pending `select-bluetooth-device` callback

  window.webContents.on('select-bluetooth-device', (event, devices, callback) => {
    event.preventDefault()
    answer = callback
    if (wanted && devices.some((device) => device.deviceId === wanted)) {
      answer = null
      callback(wanted)
      return
    }
    window.webContents.send('ble:devices', devices.map((device) => ({ id: device.deviceId, name: device.deviceName })))
  })

  const own = (event) => event.sender === window.webContents
  ipcMain.on('ble:want', (event, id) => { if (own(event)) wanted = typeof id === 'string' && id ? id : null })
  ipcMain.on('ble:select', (event, id) => {
    if (!own(event) || !answer) return
    const callback = answer
    answer = null
    callback(typeof id === 'string' ? id : '')
  })
  ipcMain.handle('zoom:get', (event) => (own(event) ? window.webContents.getZoomFactor() : 1))
  ipcMain.on('zoom:set', (event, factor) => { if (own(event)) setZoom(factor) })
  ipcMain.handle('ble:gesture', async (event) => {
    if (own(event)) await window.webContents.executeJavaScript('globalThis.__runitGesture?.()', true)
  })
  window.on('closed', () => {
    ipcMain.removeAllListeners('ble:want')
    ipcMain.removeAllListeners('ble:select')
    ipcMain.removeHandler('ble:gesture')
    ipcMain.removeHandler('zoom:get')
    ipcMain.removeAllListeners('zoom:set')
  })

  // `npm run app` serves the page with Vite; the packaged app loads the built page.
  if (process.env.RUNIT_DEV_URL) void window.loadURL(process.env.RUNIT_DEV_URL)
  else void window.loadFile(at('../dist/index.html'))
}

// One profile folder holds the page's storage (the remembered board): a second instance on it would get storage that only lives
// in memory. So one instance per profile: a second launch brings the open window forward. The dev app (`npm run app`) has a
// profile of its own, so it runs beside the installed one.
if (process.env.RUNIT_DEV_URL) app.setPath('userData', `${app.getPath('userData')}-dev`)
if (!app.requestSingleInstanceLock()) app.quit()
else {
  app.on('second-instance', () => {
    const [window] = BrowserWindow.getAllWindows()
    if (window?.isMinimized()) window.restore()
    window?.focus()
  })
  app.whenReady().then(createWindow)
}
app.on('window-all-closed', () => app.quit())
// The remembered board is in the page's localStorage, which Chromium writes to disk lazily.
app.on('before-quit', () => session.defaultSession.flushStorageData())
