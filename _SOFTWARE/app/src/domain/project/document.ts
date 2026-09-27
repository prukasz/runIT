/*
 * The project document: one JSON file holding everything the app builds a
 * program from (features.md §4.2). The file is the source of truth; the device
 * only receives what the compiler makes of it.
 *
 * IDs here are stable strings (`id`), never VM wire IDs. Objects, accessors and
 * blocks each have their own dense wire ID space on the device (0..count-1);
 * the compiler assigns those on every compile and returns a map back to these
 * IDs, so edits never renumber anything stored in the file.
 *
 * Sections so far: the object tree, BLE and data connector settings
 * (`settings`), user devices (`devices`, installed by the stored code) and
 * the devices' default settings (`setup`, contract calls run after them),
 * actions (`actions`, recorded on the board on demand), the board's autostart
 * option and frames of classes without an editor yet (`extraFrames`, kept from
 * a recovered board). Canvases and remote layouts follow as their own
 * sections.
 *
 * The program's object space can hold more than the user's tree: block-owned
 * objects (outputs, ENO) come as further ObjectSections, generated from the
 * blocks (and, until blocks exist in the app, recovered from a board). The
 * compiler numbers every section into one wire ID space, the user's objects
 * first.
 */

export const PROJECT_FORMAT = 'runit-project'
export const PROJECT_FORMAT_VERSION = 1

export interface ProjectDocument {
  readonly format: typeof PROJECT_FORMAT
  readonly format_version: number
  readonly name: string
  /** Top-level objects and folders, in display order. */
  readonly objects: readonly ObjectNode[]
  /** BLE and data connector settings; absent = the board defaults. */
  readonly settings?: ProjectSettings
  /** Start the VM after the boot replay of the stored code (the board's `prj_opts`). */
  readonly autostart?: boolean
  /** Frames of classes the app has no editor for yet, stored with the code unchanged, in order. */
  readonly extraFrames?: readonly RawFrame[]
  /** Devices the user adds on top of the board's own; the stored code installs them at boot. */
  readonly devices?: readonly ProjectDevice[]
  /** Contract sequences recorded on the board as dynamic actions (sys_actions). */
  readonly actions?: readonly ProjectAction[]
  /** Default settings: contract calls (pin modes, levels, frequencies …) the stored code runs after installing the devices. */
  readonly setup?: readonly ActionStep[]
}

// ---------------------------------------------------------------------------
// Devices and actions
// ---------------------------------------------------------------------------

/** The tile shown for a device: a named icon, or an image (data URL: PNG, JPEG, SVG). */
export interface DeviceAppearance {
  readonly icon?: string
  readonly image?: string
}

export interface ProjectDevice {
  readonly id: string
  /** The board's device ID (sys_device registry), unique among board and user devices. */
  readonly deviceId: number
  /** Device type: descriptor ID, e.g. `device_pca9685`. */
  readonly type: string
  readonly name: string
  readonly description?: string
  readonly tags: readonly string[]
  /** Install packet values by field name, without `device_id`. */
  readonly install: Readonly<Record<string, number>>
  readonly appearance?: DeviceAppearance
}

/** A board device (`board:<device ID>`) or a user device (its project ID). */
export type DeviceRef = string

export const boardDeviceRef = (deviceId: number): DeviceRef => `board:${deviceId}`

/** A contract call's values by parameter name: a number, or a list for an array field. */
export type StepValues = Readonly<Record<string, number | readonly number[]>>

/** One contract call: the device ID comes from `device` when the frames are built. */
export interface ActionStep {
  readonly id: string
  readonly device: DeviceRef
  /** Contract packet struct name, e.g. `packet_sys_io_set_level_t`. */
  readonly contract: string
  readonly values: StepValues
}

export interface ProjectAction {
  readonly id: string
  /** Dynamic action ID on the board, 1..255. */
  readonly actionId: number
  readonly name: string
  readonly steps: readonly ActionStep[]
}

/** A command frame as the board takes it: `[class][packet][payload]`, no seq byte. */
export interface RawFrame {
  readonly label: string
  readonly frame: Uint8Array
}

// ---------------------------------------------------------------------------
// Settings (editor state: what the settings screens show, saved as is)
// ---------------------------------------------------------------------------

export type BleValueFormat = 'U8' | 'U16' | 'U32' | 'I32' | 'F' | 'STR' | 'RAW'

export interface BleCharacteristicSettings {
  id: string
  name: string
  /** 16-bit hex, e.g. `0xFFE1`. */
  uuid: string
  /** The board's own (firmware); refreshed from the descriptors on load. */
  system?: boolean
  description?: string
  // GATT properties (sys_ble_char_cfg_t)
  read: boolean
  write: boolean
  writeNoResponse: boolean
  notify: boolean
  indicate: boolean
  /** Static RAM buffers, bytes; 0 = none. */
  txBufferSize: number
  rxBufferSize: number
  // Editor only
  format: BleValueFormat
  initialValue?: string
  connectorStream?: string
}

export interface BleServiceSettings {
  id: string
  name: string
  uuid: string
  system?: boolean
  isPrimary: boolean
  advertised: boolean
  characteristics: BleCharacteristicSettings[]
}

/** Editor only so far: the firmware has no commands for these yet. */
export interface BleGeneralSettings {
  deviceName: string
  advIntervalMs: number
  advFastTimeoutSec: number
  connectable: boolean
  txPowerDbm: number
  minConnIntervalMs: number
  maxConnIntervalMs: number
  connLatency: number
  supervisionTimeoutMs: number
  mtuSize: number
  securityMode: 'just_works' | 'passkey' | 'mitm'
  passkeyPin: string
}

export interface BleProfile {
  name: string
  general: BleGeneralSettings
  services: BleServiceSettings[]
}

export interface ConnectorBindingSettings {
  id: string
  provider: 'BLE' | 'UART'
  /** BLE: characteristic UUID (`0xFFE1`); UART: port (`Console (0)`, `0`). */
  endpoint: string
  direction: 'TX' | 'RX' | 'TX_RX'
}

export interface ConnectorSettings {
  id: number
  key: string
  name: string
  alias: string
  /** One byte hex, e.g. `0x02`. */
  header: string
  system: boolean
  description: string
  direction: 'TX' | 'RX' | 'TX_RX'
  maxPacketLen: number
  isSuspended: boolean
  cMacro: string
  bindings: ConnectorBindingSettings[]
}

export interface ProjectSettings {
  readonly ble: BleProfile
  readonly connectors: readonly ConnectorSettings[]
}

export const DEFAULT_BLE_GENERAL: BleGeneralSettings = {
  deviceName: 'runIT-Device',
  advIntervalMs: 100,
  advFastTimeoutSec: 30,
  connectable: true,
  txPowerDbm: 3,
  minConnIntervalMs: 15,
  maxConnIntervalMs: 30,
  connLatency: 0,
  supervisionTimeoutMs: 5000,
  mtuSize: 512,
  securityMode: 'just_works',
  passkeyPin: '123456',
}

/** Initial values: numbers for numeric types, booleans for B, a string for STR, missing = zeros. */
export type ObjectValue = readonly number[] | readonly boolean[] | string

/** A folder is a VM_OBJ_PTR object whose elements are its children, in order. */
export interface FolderNode {
  readonly kind: 'folder'
  readonly id: string
  readonly name: string
  readonly description?: string
  readonly children: readonly ObjectNode[]
  /** Track live updates / telemetry stream subscription. */
  readonly subscribed?: boolean
}

export interface ValueNode {
  readonly kind: 'value'
  readonly id: string
  readonly name: string
  readonly description?: string
  /** VM object type without `VM_OBJ_`: `U8`, `U32`, `I32`, `F`, `B`, `STR` (vm-program.generated.json types). */
  readonly type: string
  /** Elements; for STR the capacity in bytes. */
  readonly length: number
  readonly value?: ObjectValue
  /** Writable at run time (by blocks and by the app). false = a constant. */
  readonly mutable: boolean
  /** Kept across restarts by the device, by name. */
  readonly retentive: boolean
  /** Automatic type inference in the editor; absent = manual. */
  readonly typeMode?: 'auto'
  /** Track live updates / telemetry stream subscription. */
  readonly subscribed?: boolean
}

/** A non-owning tree entry pointing at a value or folder defined elsewhere. */
export interface ReferenceNode {
  readonly kind: 'reference'
  readonly id: string
  readonly name: string
  readonly description?: string
  readonly targetId: string
}

export type ObjectNode = FolderNode | ValueNode | ReferenceNode

/** Who owns a section of the object space. User objects are edited in the tree; block objects come from blocks. */
export type ObjectOwner = 'user' | 'block'

/** One slice of the program's object space, in wire ID order. */
export interface ObjectSection {
  /** Unique among the sections of one compile, e.g. `user`, `blocks`. */
  readonly key: string
  readonly owner: ObjectOwner
  readonly objects: readonly ObjectNode[]
}

/**
 * What a block pin reads or writes: an object of the tree, then steps below it
 * (compiled to a VM accessor). Each step picks an element of the object reached
 * so far; when that element is a folder's child, the next step goes into the
 * child. A path that ends on a folder's child is its slot in the folder (what
 * CLONE writes into), not the child itself.
 */
export interface ObjectPath {
  /** Project ID of a value, folder or reference (any section). */
  readonly root: string
  readonly steps?: readonly PathStep[]
}

export type PathStep =
  /** A fixed element or child position. */
  | { readonly kind: 'index'; readonly index: number }
  /** The child with this name: for data whose shape is only known at run time (CLONE copies). */
  | { readonly kind: 'name'; readonly name: string }
  /** The position is read from another path on every use (a selector, a step counter). */
  | { readonly kind: 'dynamic'; readonly index: ObjectPath }

/** The user's tree as the first section. */
export const userSection = (project: ProjectDocument): ObjectSection => ({ key: 'user', owner: 'user', objects: project.objects })

export const createProject = (name: string): ProjectDocument => ({
  format: PROJECT_FORMAT,
  format_version: PROJECT_FORMAT_VERSION,
  name,
  objects: [],
})
