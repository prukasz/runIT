import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import '@ui/styles/theme.css'
import '@ui/layout/Layout.css'
import '@ui/styles/status.css' // after Layout.css: the status colours must win over the plain button styles
import '@ui/styles/error.css'
import './App.css'
import { App } from './App'
import { AppPrefsProvider } from './appSettings/AppPrefs'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <AppPrefsProvider>
      <App />
    </AppPrefsProvider>
  </StrictMode>,
)
