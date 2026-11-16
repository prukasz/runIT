import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitest/config'

const at = (path: string) => fileURLToPath(new URL(path, import.meta.url))

export default defineConfig({
  resolve: {
    alias: {
      '@generated': at('./generated'),
      '@library': at('../library'),
      '@engine': at('./src/engine'),
      '@backend': at('./src/backend'),
      '@ui': at('./src/ui'),
    },
  },
  test: {
    pool: 'vmThreads', // the default pool fails on Node 22.23 with vitest 5.0.4 before a test runs
    include: ['tests/**/*.test.ts'],
    exclude: ['tests/hardware/**', 'node_modules/**'], // the board's own: npm run test:hw
    environment: 'node',
  },
})
