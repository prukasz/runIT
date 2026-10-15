# runit-app

The runIT app, written from scratch (the previous one is in `app/` and `legacy/app-old/`, kept only as a reference for ideas). So far: the VM engine in `src/engine/` (read `src/engine/ENGINE.md` first) and the link to the board in `src/backend/` (`src/backend/BACKEND.md`).

## Rules

- **Facts come from the firmware.** The C in `components/vm/` is the truth; `generated/` (the bundle `templates/generate_bundle.py` copies from `components/`) is what the app reads. Never `data-structures/` (legacy) and never a number or layout you cannot point to in C or in a descriptor: put the source in a comment, and a test that reads the C where it can.
- **The backend has no UI** and takes every number of the link from `generated/common/` (`src/backend/definitions.ts` is the only file that reads it). The engine does not depend on it.
- **The engine has no UI, no DOM, no connection.** It is data in, data out, with its dependencies pointing down `wire` → `catalog` → `model` → `graph` → `expression` → `compiler` → `recover`. The only file that reads `generated/` is `src/engine/generated/index.ts`.
- **A mistake in a project is a diagnostic, not an exception.**
- Aliases: `@engine/*` is `src/engine/*`, `@generated/*` is `../generated/*` (see `tsconfig.json`, `vitest.config.ts`).

## Commands

```
npm test            # vitest
npm run typecheck   # tsc --noEmit
```
