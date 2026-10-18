// Builds design/kit-data.js for the UI kit page: the page's data comes from the backend and the engine, not from the page.
//   node design/build-kit.mjs        (npm run kit)
// Vite loads the TypeScript the way the tests do (aliases @backend, @engine, @generated).
import { readFileSync, writeFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'

const at = (p) => fileURLToPath(new URL(p, import.meta.url))
const server = await createServer({
  root: at('..'),
  configFile: false,
  logLevel: 'error',
  appType: 'custom',
  server: { middlewareMode: true, fs: { allow: [at('../..')] } },
  resolve: { alias: { '@generated': at('../../generated'), '@engine': at('../src/engine'), '@backend': at('../src/backend') } },
})

try {
  const backend = await server.ssrLoadModule('/src/backend/index.ts')
  const engine = await server.ssrLoadModule('/src/engine/index.ts')
  const sample = await server.ssrLoadModule('/design/sample.ts')
  const catalog = backend.bundledServiceCatalog()

  // ---- variables: the engine's model, its limits and what it says about a tree it refuses ----
  const variables = {
    nameMax: engine.NAME_MAX,
    types: engine.VALUE_TYPES,
    tree: sample.sampleVariables,
    problemsOfTree: engine.checkVariables(sample.sampleVariables).map((p) => p.message),
    broken: sample.brokenVariables,
    problemsOfBroken: engine.checkVariables(sample.brokenVariables).map((p) => ({ id: p.id, code: p.code, message: p.message })),
  }

  // ---- devices: the catalog the board's descriptors give, and the project's own ----
  const raw = (file) => JSON.parse(readFileSync(at(`../../generated/devices/${file}`), 'utf8'))
  const types = [...catalog.devices.values()].map((d) => {
    const json = raw(`device_${d.id}.json`)
    return {
      id: d.id, label: d.label, typeId: d.typeId, tags: d.tags, contracts: d.contracts,
      pins: json.provides?.pins ? { count: json.provides.pins.count, labels: json.provides.pins.labels ?? null, modes: json.provides.pins.modes } : null,
      config: (d.config?.fields ?? []).map((f) => ({ id: f.id, label: f.label, rawType: f.rawType, pin: f.pinRef === true || f.rawType === 'pin_ref' })),
    }
  })
  const projectDevices = [
    { id: 0, type: 'gpio_esp', system: true },
    { id: 1, type: 'tca6424a', system: true },
    { id: 3, type: 'pca9685', system: true },
    { id: 4, type: 'dac53202', system: true, suspended: true },
    { id: 12, type: 'ina3221', system: true },
  ].map((d) => ({ ...d, name: sample.sampleDevices.get(d.id), typeLabel: types.find((t) => t.id === d.type)?.label }))
  const pinLabel = (type, pin) => type.pins?.labels?.[pin] ?? `Pin ${pin}`
  const pinOptions = projectDevices.flatMap((d) => {
    const type = types.find((t) => t.id === d.type)
    if (!type?.pins) return []
    const count = Math.min(type.pins.count, 24)
    return Array.from({ length: count }, (_, pin) => ({ value: `${d.id}:${pin}`, label: pinLabel(type, pin), group: d.name, hint: type.pins.modes.slice(0, 2).join(' · ') }))
  })

  // ---- errors: the records the board sent, read by the interpreter with the project's names ----
  const samples = JSON.parse(readFileSync(at('board-errors.json'), 'utf8'))
  const resolvers = { compute: sample.compiled.compute(), lamp: sample.compiled.lamp() }
  const projects = { compute: sample.computeProject(), lamp: sample.lampProject() }
  const errors = samples.map((s) => {
    const resolve = s.program
      ? backend.combineResolvers(backend.programRefs(projects[s.program], resolvers[s.program]), backend.deviceRefs(sample.sampleDevices))
      : backend.deviceRefs(sample.sampleDevices)
    const interpreter = new backend.ErrorInterpreter({ resolve })
    const report = interpreter.read(Uint8Array.from(Buffer.from(s.hex, 'hex')))
    return {
      id: s.id, title: s.title, severity: report.severity, message: report.message, where: report.where, command: report.command?.label ?? null,
      chain: report.chain.map((n) => ({ name: n.name, context: n.context, message: n.message })),
      refs: report.refs.map((r) => ({ key: backend.refKey(r), kind: r.kind, target: interpreter.target(r) ?? null })).filter((r) => r.target),
    }
  })

  // ---- blocks: the user's instances of the two programs, with the pins the compiler gave them ----
  const blockTypes = backend.bundledBlockTypes()
  const blocks = ['compute', 'lamp'].flatMap((name) => {
    const result = resolvers[name]
    return projects[name].blocks.map((b) => {
      const ir = result.ir.blocks.find((x) => x.id === result.ids.blocks.get(b.id))
      return { id: b.id, name: b.name, type: blockTypes.get(ir.type) ?? b.type, program: name, pins: { input: ir.inputs.length, output: ir.outputs.length, enable: ir.enables.length } }
    })
  })

  // ---- the link ----
  const link = { states: ['idle', 'connecting', 'connected', 'reconnecting', 'closed'], streams: backend.STREAM_NAMES }

  const data = { variables, devices: { types, project: projectDevices, pinOptions }, blocks, errors, link }
  writeFileSync(at('kit-data.js'), `// generated by design/build-kit.mjs from the backend, the engine and board-errors.json: do not edit\nwindow.KIT = ${JSON.stringify(data, null, 1)}\n`)
  console.log(`kit-data.js: ${types.length} device types, ${projectDevices.length} project devices, ${pinOptions.length} pins, ${errors.length} errors, ${variables.tree.length} top variables`)
} finally {
  await server.close()
}
