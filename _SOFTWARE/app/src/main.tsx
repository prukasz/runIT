import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
import BleTestApp from './BleTestApp.tsx'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BleTestApp />
  </StrictMode>,
)
