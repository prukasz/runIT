import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

const at = (path: string) => fileURLToPath(new URL(path, import.meta.url))

// The ports of the dev server: the PCB's app and the devkit's (`--mode devkit`, .env.devkit) run side by side.
const DEV_PORTS = { pcb: 5180, devkit: 5181 } as const

// The app (`npm run dev`, `npm run build`; `--mode devkit` for a devkit board). The tests have their own config (vitest.config.ts).
export default defineConfig(({ mode }) => ({
  // Relative asset paths: the desktop app loads the built page from a file (electron/main.mjs).
  base: './',
  plugins: [react()],
  resolve: {
    alias: { '@generated': at('./generated'), '@library': at('../library'), '@engine': at('./src/engine'), '@backend': at('./src/backend'), '@ui': at('./src/ui') },
  },
  server: {
    // A fixed port away from Vite's default, which other local projects use. localhost is a secure context (Web Bluetooth).
    port: mode === 'devkit' ? DEV_PORTS.devkit : DEV_PORTS.pcb,
    strictPort: true,
    // The hardware library (the parts of the Devices canvas) lives beside the app, in _SOFTWARE/library.
    fs: { allow: ['.', '../library'] },
  },
}))
