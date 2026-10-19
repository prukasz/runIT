import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

const at = (path: string) => fileURLToPath(new URL(path, import.meta.url))

// The app (`npm run dev`, `npm run build`). The tests have their own config (vitest.config.ts).
export default defineConfig({
  // Relative asset paths: the desktop app loads the built page from a file (electron/main.mjs).
  base: './',
  plugins: [react()],
  resolve: {
    alias: { '@generated': at('./generated'), '@engine': at('./src/engine'), '@backend': at('./src/backend'), '@ui': at('./src/ui') },
  },
  server: {
    // A fixed port away from Vite's default, which other local projects use. localhost is a secure context (Web Bluetooth).
    port: 5180,
    strictPort: true,
    fs: { allow: ['.'] },
  },
})
