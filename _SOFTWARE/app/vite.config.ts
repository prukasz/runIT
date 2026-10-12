import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), tailwindcss()],
  resolve: {
    // The generated firmware descriptors (JSON) next to the app, in the repo's data-structures/.
    alias: { '@data-structures': fileURLToPath(new URL('../data-structures', import.meta.url)) },
  },
  server: {
    // Fixed port away from Vite's default 5173, which other local projects use. localhost is a secure context (Web Bluetooth).
    port: 5180,
    strictPort: true,
    fs: { allow: ['.', '../data-structures'] },
  },
})
