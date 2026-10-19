import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import '@ui/styles/theme.css'
import '@ui/layout/Layout.css'
import '@ui/styles/status.css' // after Layout.css: the status colours must win over the plain button styles
import '@ui/styles/error.css'
import './App.css'
import { appBoardKind } from '@backend'
import { App } from './App'

// A devkit board (npm run dev:devkit / app:devkit) says so in the window's title.
if (appBoardKind() === 'devkit') document.title = 'runIT (devkit)'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
