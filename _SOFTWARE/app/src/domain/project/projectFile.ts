import { DEFAULT_BLE_GENERAL, PROJECT_FORMAT, PROJECT_FORMAT_VERSION } from './document'
import type {
  BleCharacteristicSettings,
  BleGeneralSettings,
  BleProfile,
  BleServiceSettings,
  BleValueFormat,
  ConnectorBindingSettings,
  ConnectorSettings,
  ActionStep,
  CanvasBlock,
  DeviceAppearance,
  FolderNode,
  ObjectNode,
  ObjectPath,
  ObjectValue,
  PathStep,
  ProjectAction,
  ProjectCanvas,
  ProjectDevice,
  ProjectDocument,
  ProjectSettings,
  RawFrame,
  ReferenceNode,
  ValueNode,
} from './document'

/*
 * Load and save the project document. Loading checks the shape only (every
 * field present, right JSON type, unique IDs); whether the objects fit the
 * firmware is the compiler's check, so a file with a too-long name still opens
 * and can be fixed in the editor. The same goes for settings: the settings
 * planner checks UUIDs, headers and bindings.
 */

export class ProjectFormatError extends Error {
  /** JSON path of the offending value, e.g. `objects[2].children[0].length`. */
  readonly path: string

  constructor(path: string, message: string) {
    super(`${path}: ${message}`)
    this.name = 'ProjectFormatError'
    this.path = path
  }
}

type Json = unknown

const isRecord = (value: Json): value is Record<string, Json> => typeof value === 'object' && value !== null && !Array.isArray(value)

const record = (value: Json, path: string): Record<string, Json> => {
  if (!isRecord(value)) throw new ProjectFormatError(path, 'expected an object')
  return value
}

const string = (value: Json, path: string): string => {
  if (typeof value !== 'string') throw new ProjectFormatError(path, 'expected a string')
  return value
}

const optionalString = (value: Json, path: string): string | undefined => (value === undefined ? undefined : string(value, path))

const boolean = (value: Json, path: string): boolean => {
  if (typeof value !== 'boolean') throw new ProjectFormatError(path, 'expected true or false')
  return value
}

const optionalFlag = (value: Json, path: string): { subscribed?: boolean } => (value === undefined ? {} : { subscribed: boolean(value, path) })

const count = (value: Json, path: string): number => {
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) throw new ProjectFormatError(path, 'expected a whole number >= 0')
  return value
}

const integer = (value: Json, path: string): number => {
  if (typeof value !== 'number' || !Number.isSafeInteger(value)) throw new ProjectFormatError(path, 'expected a whole number')
  return value
}

const oneOf = <T extends string>(value: Json, path: string, choices: readonly T[]): T => {
  if (typeof value !== 'string' || !choices.includes(value as T)) throw new ProjectFormatError(path, `expected one of ${choices.join(', ')}`)
  return value as T
}

/** `{ key: value }` only when the value is present. */
const optional = <K extends string, V>(key: K, value: V | undefined): { [P in K]?: V } => (value === undefined ? {} : ({ [key]: value } as { [P in K]?: V }))

const optionalBoolean = (value: Json, path: string): boolean | undefined => (value === undefined ? undefined : boolean(value, path))

const array = (value: Json, path: string): readonly Json[] => {
  if (!Array.isArray(value)) throw new ProjectFormatError(path, 'expected an array')
  return value
}

const objectValue = (value: Json, path: string): ObjectValue | undefined => {
  if (value === undefined || typeof value === 'string') return value
  const entries = array(value, path)
  if (entries.every((entry) => typeof entry === 'boolean')) return entries as boolean[]
  entries.forEach((entry, index) => {
    if (typeof entry !== 'number' || !Number.isFinite(entry)) throw new ProjectFormatError(`${path}[${index}]`, 'expected a number (or all booleans)')
  })
  return entries as number[]
}

/** One tree node; `ids` collects IDs across the whole file so duplicates are caught. */
export const parseNode = (value: Json, path: string, ids: Set<string>): ObjectNode => {
  const node = record(value, path)
  const id = string(node.id, `${path}.id`)
  if (!id) throw new ProjectFormatError(`${path}.id`, 'is empty')
  if (ids.has(id)) throw new ProjectFormatError(`${path}.id`, `'${id}' is used twice`)
  ids.add(id)
  const name = string(node.name, `${path}.name`)
  const description = optionalString(node.description, `${path}.description`)
  const kind = string(node.kind, `${path}.kind`)
  if (kind === 'folder') {
    const children = array(node.children, `${path}.children`).map((child, index) => parseNode(child, `${path}.children[${index}]`, ids))
    const subscribed = optionalFlag(node.subscribed, `${path}.subscribed`)
    const folder: FolderNode = { kind, id, name, children, ...subscribed }
    return description === undefined ? folder : { ...folder, description }
  }
  if (kind === 'reference') {
    const reference: ReferenceNode = { kind, id, name, targetId: string(node.targetId, `${path}.targetId`), ...(description === undefined ? {} : { description }) }
    return reference
  }
  if (kind === 'value') {
    const parsed: ValueNode = {
      kind,
      id,
      name,
      type: string(node.type, `${path}.type`),
      length: count(node.length, `${path}.length`),
      mutable: boolean(node.mutable, `${path}.mutable`),
      retentive: boolean(node.retentive, `${path}.retentive`),
    }
    const initial = objectValue(node.value, `${path}.value`)
    if (node.typeMode !== undefined && node.typeMode !== 'auto') throw new ProjectFormatError(`${path}.typeMode`, "expected 'auto'")
    const typeMode = node.typeMode === 'auto' ? { typeMode: 'auto' as const } : {}
    const subscribed = optionalFlag(node.subscribed, `${path}.subscribed`)
    return { ...parsed, ...typeMode, ...subscribed, ...(description === undefined ? {} : { description }), ...(initial === undefined ? {} : { value: initial }) }
  }
  throw new ProjectFormatError(`${path}.kind`, `unknown kind '${kind}' (folder, value or reference)`)
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

const BLE_FORMATS: readonly BleValueFormat[] = ['U8', 'U16', 'U32', 'I32', 'F', 'STR', 'RAW']
const DIRECTIONS = ['TX', 'RX', 'TX_RX'] as const

const parseCharacteristic = (value: Json, path: string): BleCharacteristicSettings => {
  const char = record(value, path)
  return {
    id: string(char.id, `${path}.id`),
    name: string(char.name, `${path}.name`),
    uuid: string(char.uuid, `${path}.uuid`),
    ...optional('system', optionalBoolean(char.system, `${path}.system`)),
    ...optional('description', optionalString(char.description, `${path}.description`)),
    read: boolean(char.read, `${path}.read`),
    write: boolean(char.write, `${path}.write`),
    writeNoResponse: boolean(char.writeNoResponse, `${path}.writeNoResponse`),
    notify: boolean(char.notify, `${path}.notify`),
    indicate: boolean(char.indicate, `${path}.indicate`),
    txBufferSize: count(char.txBufferSize, `${path}.txBufferSize`),
    rxBufferSize: count(char.rxBufferSize, `${path}.rxBufferSize`),
    format: oneOf(char.format, `${path}.format`, BLE_FORMATS),
    ...optional('initialValue', optionalString(char.initialValue, `${path}.initialValue`)),
    ...optional('connectorStream', optionalString(char.connectorStream, `${path}.connectorStream`)),
  }
}

const parseService = (value: Json, path: string): BleServiceSettings => {
  const service = record(value, path)
  return {
    id: string(service.id, `${path}.id`),
    name: string(service.name, `${path}.name`),
    uuid: string(service.uuid, `${path}.uuid`),
    ...optional('system', optionalBoolean(service.system, `${path}.system`)),
    isPrimary: boolean(service.isPrimary, `${path}.isPrimary`),
    advertised: boolean(service.advertised, `${path}.advertised`),
    characteristics: array(service.characteristics, `${path}.characteristics`).map((char, index) => parseCharacteristic(char, `${path}.characteristics[${index}]`)),
  }
}

/** Every key of BleGeneralSettings, with the JSON type of its default. */
const parseGeneral = (value: Json, path: string): BleGeneralSettings => {
  const general = record(value, path)
  const out: Record<string, unknown> = {}
  for (const [key, fallback] of Object.entries(DEFAULT_BLE_GENERAL)) {
    const entry = general[key]
    if (typeof entry !== typeof fallback) throw new ProjectFormatError(`${path}.${key}`, `expected a ${typeof fallback}`)
    out[key] = entry
  }
  oneOf(out.securityMode, `${path}.securityMode`, ['just_works', 'passkey', 'mitm'] as const)
  return out as unknown as BleGeneralSettings
}

const parseBinding = (value: Json, path: string): ConnectorBindingSettings => {
  const binding = record(value, path)
  return {
    id: string(binding.id, `${path}.id`),
    provider: oneOf(binding.provider, `${path}.provider`, ['BLE', 'UART'] as const),
    endpoint: string(binding.endpoint, `${path}.endpoint`),
    direction: oneOf(binding.direction, `${path}.direction`, DIRECTIONS),
  }
}

const parseConnector = (value: Json, path: string): ConnectorSettings => {
  const connector = record(value, path)
  return {
    id: count(connector.id, `${path}.id`),
    key: string(connector.key, `${path}.key`),
    name: string(connector.name, `${path}.name`),
    alias: string(connector.alias, `${path}.alias`),
    header: string(connector.header, `${path}.header`),
    system: boolean(connector.system, `${path}.system`),
    description: string(connector.description, `${path}.description`),
    direction: oneOf(connector.direction, `${path}.direction`, DIRECTIONS),
    maxPacketLen: integer(connector.maxPacketLen, `${path}.maxPacketLen`),
    isSuspended: boolean(connector.isSuspended, `${path}.isSuspended`),
    cMacro: string(connector.cMacro, `${path}.cMacro`),
    bindings: array(connector.bindings, `${path}.bindings`).map((binding, index) => parseBinding(binding, `${path}.bindings[${index}]`)),
  }
}

export const parseSettings = (value: Json, path: string): ProjectSettings => {
  const settings = record(value, path)
  const ble = record(settings.ble, `${path}.ble`)
  const profile: BleProfile = {
    name: string(ble.name, `${path}.ble.name`),
    general: parseGeneral(ble.general, `${path}.ble.general`),
    services: array(ble.services, `${path}.ble.services`).map((service, index) => parseService(service, `${path}.ble.services[${index}]`)),
  }
  return { ble: profile, connectors: array(settings.connectors, `${path}.connectors`).map((connector, index) => parseConnector(connector, `${path}.connectors[${index}]`)) }
}

const parseFrames = (value: Json, path: string): RawFrame[] =>
  array(value, path).map((entry, index) => {
    const item = record(entry, `${path}[${index}]`)
    const hex = string(item.frame, `${path}[${index}].frame`).trim()
    const tokens = hex ? hex.split(/\s+/) : []
    if (tokens.length < 2 || !tokens.every((token) => /^[0-9a-f]{2}$/i.test(token))) throw new ProjectFormatError(`${path}[${index}].frame`, "expected hex bytes 'cc pp ...', class and packet at least")
    return { label: string(item.label, `${path}[${index}].label`), frame: Uint8Array.from(tokens, (token) => Number.parseInt(token, 16)) }
  })

// ---------------------------------------------------------------------------
// Devices and actions
// ---------------------------------------------------------------------------

const numberRecord = (value: Json, path: string): Record<string, number> => {
  const entries = record(value, path)
  const out: Record<string, number> = {}
  for (const [key, entry] of Object.entries(entries)) {
    if (typeof entry !== 'number' || !Number.isFinite(entry)) throw new ProjectFormatError(`${path}.${key}`, 'expected a number')
    out[key] = entry
  }
  return out
}

/** Step values: numbers, or arrays of numbers for array fields. */
const stepValues = (value: Json, path: string): Record<string, number | number[]> => {
  const entries = record(value, path)
  const out: Record<string, number | number[]> = {}
  for (const [key, entry] of Object.entries(entries)) {
    if (Array.isArray(entry)) {
      entry.forEach((item, index) => {
        if (typeof item !== 'number' || !Number.isFinite(item)) throw new ProjectFormatError(`${path}.${key}[${index}]`, 'expected a number')
      })
      out[key] = entry as number[]
    } else if (typeof entry === 'number' && Number.isFinite(entry)) out[key] = entry
    else throw new ProjectFormatError(`${path}.${key}`, 'expected a number or a list of numbers')
  }
  return out
}

const parseAppearance = (value: Json, path: string): DeviceAppearance => {
  const appearance = record(value, path)
  return { ...optional('icon', optionalString(appearance.icon, `${path}.icon`)), ...optional('image', optionalString(appearance.image, `${path}.image`)) }
}

export const parseDeviceAliases = (value: Json, path: string): Readonly<Record<string, string>> => {
  if (value === undefined) return {}
  return Object.fromEntries(Object.entries(record(value, path)).flatMap(([ref, value]) => {
    if (!ref) throw new ProjectFormatError(path, 'device reference is empty')
    const alias = string(value, `${path}.${ref}`).trim()
    return alias ? [[ref, alias]] : []
  }))
}

export const parseDevices = (value: Json, path: string): ProjectDevice[] => {
  const ids = new Set<string>()
  const deviceIds = new Set<number>()
  return array(value, path).map((entry, index) => {
    const at = `${path}[${index}]`
    const device = record(entry, at)
    const id = string(device.id, `${at}.id`)
    if (ids.has(id)) throw new ProjectFormatError(`${at}.id`, `'${id}' is used twice`)
    ids.add(id)
    const deviceId = count(device.deviceId, `${at}.deviceId`)
    if (deviceIds.has(deviceId)) throw new ProjectFormatError(`${at}.deviceId`, `device ID ${deviceId} is used twice`)
    deviceIds.add(deviceId)
    return {
      id,
      deviceId,
      type: string(device.type, `${at}.type`),
      name: string(device.name, `${at}.name`),
      ...optional('description', optionalString(device.description, `${at}.description`)),
      tags: array(device.tags, `${at}.tags`).map((tag, tagIndex) => string(tag, `${at}.tags[${tagIndex}]`)),
      install: numberRecord(device.install, `${at}.install`),
      ...optional('appearance', device.appearance === undefined ? undefined : parseAppearance(device.appearance, `${at}.appearance`)),
    }
  })
}

const parseStep = (value: Json, path: string): ActionStep => {
  const step = record(value, path)
  return { id: string(step.id, `${path}.id`), device: string(step.device, `${path}.device`), contract: string(step.contract, `${path}.contract`), values: stepValues(step.values, `${path}.values`) }
}

export const parseSetup = (value: Json, path: string): ActionStep[] => {
  const ids = new Set<string>()
  return array(value, path).map((entry, index) => {
    const step = parseStep(entry, `${path}[${index}]`)
    if (ids.has(step.id)) throw new ProjectFormatError(`${path}[${index}].id`, `'${step.id}' is used twice`)
    ids.add(step.id)
    return step
  })
}

export const parseActions = (value: Json, path: string): ProjectAction[] => {
  const actionIds = new Set<number>()
  return array(value, path).map((entry, index) => {
    const at = `${path}[${index}]`
    const action = record(entry, at)
    const actionId = count(action.actionId, `${at}.actionId`)
    if (actionId < 1 || actionId > 255) throw new ProjectFormatError(`${at}.actionId`, 'expected 1..255')
    if (actionIds.has(actionId)) throw new ProjectFormatError(`${at}.actionId`, `action ID ${actionId} is used twice`)
    actionIds.add(actionId)
    return {
      id: string(action.id, `${at}.id`),
      actionId,
      name: string(action.name, `${at}.name`),
      steps: array(action.steps, `${at}.steps`).map((step, stepIndex) => parseStep(step, `${at}.steps[${stepIndex}]`)),
    }
  })
}

const finite = (value: Json, path: string): number => {
  if (typeof value !== 'number' || !Number.isFinite(value)) throw new ProjectFormatError(path, 'expected a number')
  return value
}

const parsePath = (value: Json, path: string): ObjectPath => {
  const entry = record(value, path)
  const steps = entry.steps === undefined ? undefined : array(entry.steps, `${path}.steps`).map((step, index) => parsePathStep(step, `${path}.steps[${index}]`))
  return { root: string(entry.root, `${path}.root`), ...optional('steps', steps) }
}

const parsePathStep = (value: Json, path: string): PathStep => {
  const step = record(value, path)
  const kind = oneOf(step.kind, `${path}.kind`, ['index', 'name', 'dynamic'] as const)
  if (kind === 'index') return { kind, index: count(step.index, `${path}.index`) }
  if (kind === 'name') return { kind, name: string(step.name, `${path}.name`) }
  return { kind, index: parsePath(step.index, `${path}.index`) }
}

const parseCanvasBlock = (value: Json, path: string): CanvasBlock => {
  const block = record(value, path)
  const settings = block.settings === undefined ? undefined : Object.fromEntries(Object.entries(record(block.settings, `${path}.settings`)).map(([name, entry]) => [name, typeof entry === 'string' ? entry : finite(entry, `${path}.settings.${name}`)]))
  const expression = block.expression === undefined ? undefined : (() => {
    const at = `${path}.expression`
    const entry = record(block.expression, at)
    const constants = entry.constants === undefined ? undefined : array(entry.constants, `${at}.constants`).map((constant, index) => finite(constant, `${at}.constants[${index}]`))
    const code = array(entry.code, `${at}.code`).map((token, index) => (typeof token === 'string' ? token : finite(token, `${at}.code[${index}]`)))
    return { ...optional('constants', constants), code }
  })()
  return {
    id: string(block.id, `${path}.id`),
    type: string(block.type, `${path}.type`),
    x: finite(block.x, `${path}.x`),
    y: finite(block.y, `${path}.y`),
    ...optional('view', block.view === undefined ? undefined : oneOf(block.view, `${path}.view`, ['simple', 'detailed'] as const)),
    ...optional('name', block.name === undefined ? undefined : string(block.name, `${path}.name`)),
    ...optional('outputAliases', block.outputAliases === undefined ? undefined : array(block.outputAliases, `${path}.outputAliases`).map((entry, index) => entry === null ? null : string(entry, `${path}.outputAliases[${index}]`))),
    ...optional('dynamicInputs', block.dynamicInputs === undefined ? undefined : array(block.dynamicInputs, `${path}.dynamicInputs`).map((entry, index) => count(entry, `${path}.dynamicInputs[${index}]`))),
    ...optional('inputs', block.inputs === undefined ? undefined : array(block.inputs, `${path}.inputs`).map((entry, index) => (entry === null ? null : parsePath(entry, `${path}.inputs[${index}]`)))),
    ...optional('outputs', block.outputs === undefined ? undefined : array(block.outputs, `${path}.outputs`).map((entry, index) => (entry === null ? null : string(entry, `${path}.outputs[${index}]`)))),
    ...optional('enables', block.enables === undefined ? undefined : array(block.enables, `${path}.enables`).map((entry, index) => parsePath(entry, `${path}.enables[${index}]`))),
    ...optional('enableMode', block.enableMode === undefined ? undefined : oneOf(block.enableMode, `${path}.enableMode`, ['any', 'all'] as const)),
    ...optional('inheritGates', optionalBoolean(block.inheritGates, `${path}.inheritGates`) === false ? (false as const) : undefined),
    ...optional('onError', block.onError === undefined ? undefined : oneOf(block.onError, `${path}.onError`, ['stop', 'continue'] as const)),
    ...optional('eno', optionalBoolean(block.eno, `${path}.eno`)),
    ...optional('settings', settings),
    ...optional('expression', expression),
    ...optional('body', block.body === undefined ? undefined : count(block.body, `${path}.body`)),
  }
}

/** Canvases in execution order; canvas IDs and block IDs are unique (the blocks of all canvases are one program). */
export const parseCanvases = (value: Json, path: string): ProjectCanvas[] => {
  const ids = new Set<string>()
  const blockIds = new Set<string>()
  return array(value, path).map((entry, index) => {
    const at = `${path}[${index}]`
    const canvas = record(entry, at)
    const id = string(canvas.id, `${at}.id`)
    if (ids.has(id)) throw new ProjectFormatError(`${at}.id`, `'${id}' is used twice`)
    ids.add(id)
    const blocks = array(canvas.blocks, `${at}.blocks`).map((block, blockIndex) => {
      const parsed = parseCanvasBlock(block, `${at}.blocks[${blockIndex}]`)
      if (blockIds.has(parsed.id)) throw new ProjectFormatError(`${at}.blocks[${blockIndex}].id`, `block '${parsed.id}' is used twice`)
      blockIds.add(parsed.id)
      return parsed
    })
    const variables = canvas.variables === undefined ? undefined : array(canvas.variables, `${at}.variables`).map((variable, variableIndex) => {
      const where = `${at}.variables[${variableIndex}]`
      const entry = record(variable, where)
      return { id: string(entry.id, `${where}.id`), path: parsePath(entry.path, `${where}.path`), x: finite(entry.x, `${where}.x`), y: finite(entry.y, `${where}.y`) }
    })
    return { id, name: string(canvas.name, `${at}.name`), ...optional('disabled', optionalBoolean(canvas.disabled, `${at}.disabled`)), blocks, ...optional('variables', variables?.length ? variables : undefined) }
  })
}

const frameHex = (frame: Uint8Array): string => Array.from(frame, (byte) => byte.toString(16).padStart(2, '0')).join(' ')

/** Parse a project file. Throws ProjectFormatError naming the first bad value. */
export const parseProject = (text: string): ProjectDocument => {
  let root: Json
  try {
    root = JSON.parse(text)
  } catch (error) {
    throw new ProjectFormatError('$', `not JSON (${error instanceof Error ? error.message : String(error)})`)
  }
  const doc = record(root, '$')
  if (doc.format !== PROJECT_FORMAT) throw new ProjectFormatError('format', `expected '${PROJECT_FORMAT}'`)
  const version = count(doc.format_version, 'format_version')
  if (version !== PROJECT_FORMAT_VERSION) throw new ProjectFormatError('format_version', `${version} is not supported (this app reads ${PROJECT_FORMAT_VERSION})`)
  const ids = new Set<string>()
  return {
    format: PROJECT_FORMAT,
    format_version: version,
    name: string(doc.name, 'name'),
    ...optional('autostart', optionalBoolean(doc.autostart, 'autostart')),
    objects: array(doc.objects, 'objects').map((node, index) => parseNode(node, `objects[${index}]`, ids)),
    ...optional('settings', doc.settings === undefined ? undefined : parseSettings(doc.settings, 'settings')),
    ...optional('devices', doc.devices === undefined ? undefined : parseDevices(doc.devices, 'devices')),
    ...optional('deviceAliases', doc.deviceAliases === undefined ? undefined : parseDeviceAliases(doc.deviceAliases, 'deviceAliases')),
    ...optional('actions', doc.actions === undefined ? undefined : parseActions(doc.actions, 'actions')),
    ...optional('setup', doc.setup === undefined ? undefined : parseSetup(doc.setup, 'setup')),
    ...optional('canvases', doc.canvases === undefined ? undefined : parseCanvases(doc.canvases, 'canvases')),
    ...optional('extraFrames', doc.extra_frames === undefined ? undefined : parseFrames(doc.extra_frames, 'extra_frames')),
  }
}

// Fixed key order, so saved files diff cleanly.
export const nodeJson = (node: ObjectNode): Record<string, Json> => {
  const head = { id: node.id, kind: node.kind, name: node.name, ...(node.description === undefined ? {} : { description: node.description }) }
  const subscribed = node.kind !== 'reference' && node.subscribed !== undefined ? { subscribed: node.subscribed } : {}
  if (node.kind === 'folder') return { ...head, ...subscribed, children: node.children.map(nodeJson) }
  if (node.kind === 'reference') return { ...head, targetId: node.targetId }
  return { ...head, type: node.type, ...(node.typeMode ? { typeMode: node.typeMode } : {}), length: node.length, mutable: node.mutable, retentive: node.retentive, ...subscribed, ...(node.value === undefined ? {} : { value: node.value }) }
}

/** The project as file text: 2-space JSON, fixed key order (settings in their interfaces' order), trailing newline. */
export const serializeProject = (project: ProjectDocument): string =>
  `${JSON.stringify(
    {
      format: PROJECT_FORMAT,
      format_version: PROJECT_FORMAT_VERSION,
      name: project.name,
      ...optional('autostart', project.autostart),
      objects: project.objects.map(nodeJson),
      // Rebuilt by the parser: its key order, whatever order the editors built the objects in.
      ...optional('settings', project.settings && parseSettings(JSON.parse(JSON.stringify(project.settings)), 'settings')),
      // Rebuilt by the parser too: fixed key order.
      ...optional('devices', project.devices && parseDevices(JSON.parse(JSON.stringify(project.devices)), 'devices')),
      ...optional('deviceAliases', project.deviceAliases && parseDeviceAliases(project.deviceAliases, 'deviceAliases')),
      ...optional('actions', project.actions && parseActions(JSON.parse(JSON.stringify(project.actions)), 'actions')),
      ...optional('setup', project.setup && parseSetup(JSON.parse(JSON.stringify(project.setup)), 'setup')),
      ...optional('canvases', project.canvases && parseCanvases(JSON.parse(JSON.stringify(project.canvases)), 'canvases')),
      ...optional('extra_frames', project.extraFrames?.map((entry) => ({ label: entry.label, frame: frameHex(entry.frame) }))),
    },
    null,
    2,
  )}\n`
