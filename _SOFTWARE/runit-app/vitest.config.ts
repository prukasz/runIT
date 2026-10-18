import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitest/config'

const at = (path: string) => fileURLToPath(new URL(path, import.meta.url))

export default defineConfig({
  resolve: {
    alias: {
      '@generated': at('../generated'),
      '@engine': at('./src/engine'),
      '@backend': at('./src/backend'),
      '@ui': at('./src/ui'),
    },
  },
  test: {
    include: ['tests/**/*.test.ts'],
    exclude: ['tests/hardware/**', 'node_modules/**'], // the board's own: npm run test:hw
    environment: 'node',
  },
})
