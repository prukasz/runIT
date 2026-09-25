import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// Generated firmware descriptors live next to the app, in the repo's data-structures/.
const dataStructures = fileURLToPath(new URL('../data-structures', import.meta.url))

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: { '@data-structures': dataStructures },
  },
  server: {
    // Fixed port away from Vite's default 5173, which other local projects use.
    port: 5180,
    strictPort: true,
    fs: { allow: ['.', dataStructures] },
  },
})
