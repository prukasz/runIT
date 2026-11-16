import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vitest/config'

const at = (path: string) => fileURLToPath(new URL(path, import.meta.url))

/* The backend against a real board (tests/hardware/): `npm run test:hw`. One board, so one file at a time, in order. */
export default defineConfig({
  resolve: {
    alias: {
      '@generated': at('./generated'),
      '@engine': at('./src/engine'),
      '@backend': at('./src/backend'),
    },
  },
  test: {
    pool: 'vmThreads', // the default pool fails on Node 22.23 with vitest 5.0.4 before a test runs
    include: ['tests/hardware/**/*.hw.test.ts'],
    environment: 'node',
    fileParallelism: false,
    testTimeout: 60_000,
    hookTimeout: 60_000,
    sequence: { files: 'alphabetical' } as never,
  },
})
