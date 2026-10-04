import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { defineConfig } from 'vite'
import { boardLibrary } from './vite/boardLibrary.ts'
import { componentFiles } from './vite/componentFiles.ts'

// Generated firmware descriptors live next to the app, in the repo's data-structures/.
const dataStructures = fileURLToPath(new URL('../data-structures', import.meta.url))
// Each VM block's display.json (hand-written) and content.json (generated from its header) sit in its firmware folder.
const vmBlocks = fileURLToPath(new URL('../components/VM/blocks', import.meta.url))
// Saved pictures and anchor layouts of the devices (the anchor editor writes them) are bundled for the board canvas.
const deviceFolders = fileURLToPath(new URL('../components/devices', import.meta.url))
// Board templates saved by the board creator (the library).
const boardFolder = fileURLToPath(new URL('../components/boards', import.meta.url))

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), tailwindcss(), componentFiles(deviceFolders), boardLibrary(boardFolder)],
  resolve: {
    alias: { '@data-structures': dataStructures, '@vm-blocks': vmBlocks },
  },
  server: {
    // Fixed port away from Vite's default 5173, which other local projects use.
    port: 5180,
    strictPort: true,
    fs: { allow: ['.', dataStructures, vmBlocks, deviceFolders, boardFolder] },
  },
})
