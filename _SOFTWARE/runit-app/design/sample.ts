import type { Variable } from '@engine'
import { blk, build, num, project, read } from '../tests/engine/helpers'

/*
 * The sample project of the UI kit: what the page shows is built from the backend's own types (engine `Variable`, the device
 * catalog, `ErrorReport`), never typed into the page. The same program is compiled for the board capture (the kit-capture test
 * in tests/hardware) and for the page (build-kit.mjs), so an error record's numbers resolve to the same names in both.
 */

/** The variables of the kit's project: every kind and flag the engine model has. */
export const sampleVariables: Variable[] = [
  { kind: 'value', id: 'run', name: 'Run', type: 'bool', count: 1, initial: true },
  { kind: 'value', id: 'speed', name: 'drive-speed', type: 'f32', count: 1, description: 'Set by the remote' },
  { kind: 'value', id: 'limit', name: 'current-limit', type: 'u32', count: 1, initial: 2000, retained: true },
  {
    kind: 'folder', id: 'angles', name: 'angles', children: [
      { kind: 'value', id: 'arm1', name: 'arm1', type: 'f32', count: 1 },
      { kind: 'value', id: 'arm2', name: 'arm2', type: 'f32', count: 1 },
      { kind: 'value', id: 'arm3', name: 'arm3', type: 'f32', count: 1 },
    ],
  },
  { kind: 'value', id: 'samples', name: 'samples', type: 'f32', count: 8 },
  { kind: 'value', id: 'label', name: 'label', type: 'str', count: 16, initial: 'runIT' },
  { kind: 'value', id: 'estop', name: 'Emergency-stop', type: 'bool', count: 1, initial: false, protected: true },
  { kind: 'value', id: 'version', name: 'version', type: 'u32', count: 1, initial: 3, writable: false },
  { kind: 'value', id: 'steps', name: 'steps', type: 'i32', count: 1 },
  { kind: 'value', id: 'mode', name: 'mode', type: 'u8', count: 1, initial: 1 },
  { kind: 'reference', id: 'alias', name: 'speed-alias', target: 'speed' },
  { kind: 'reference', id: 'unset', name: 'unset-ref', target: null },
]

/** A tree the engine refuses: what `checkVariables` says is what the page shows under the fields. */
export const brokenVariables: Variable[] = [
  { kind: 'value', id: 'a', name: 'speed', type: 'f32', count: 1 },
  { kind: 'value', id: 'b', name: 'speed', type: 'f32', count: 1 },
  { kind: 'value', id: 'c', name: 'a-name-longer-than-fifteen', type: 'f32', count: 1 },
  { kind: 'value', id: 'd', name: 'label', type: 'str', count: 4, initial: 'too long for it' },
  { kind: 'reference', id: 'e', name: 'lost-ref', target: 'gone' },
]

/** The devices of the kit's project by id: the names the user gave them (the errors and pills show these). */
export const sampleDevices = new Map<number, string>([
  [0, 'ESP32-S3 GPIO'],
  [1, 'Expander'],
  [3, 'Servo driver'],
  [4, 'Analog out'],
  [12, 'Power monitor'],
])

/** Program 1: an expression block, `z = x * 2 + y`. */
export const computeProject = () =>
  project(
    [num('x', 'Speed'), num('y', 'Offset', { initial: 3 }), num('z', 'Result')],
    [blk('e', 'expr', { name: 'Compute', groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })],
  )

/** Program 2: an IO block on a pin the expander does not have (the board raises it while the program runs). */
export const lampProject = () => project([num('x', 'Speed')], [blk('t', 'io', { name: 'Lamp', variant: 'toggle', device: 1, values: { pin: 99 } })])

export const compiled = {
  compute: () => build(computeProject()),
  lamp: () => build(lampProject()),
}
