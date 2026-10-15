export { hasErrors } from './program/diagnostics'
export type { Diagnostic } from './program/diagnostics'
export { layoutVariables } from './variables/layout'
export type { VariableLayout } from './variables/layout'
export {
  appendStep,
  cutSteps,
  dimensionsOf,
  draftForVariable,
  draftOf,
  EMPTY_DRAFT,
  formatDraft,
  inspectDraft,
  nextSteps,
  parseDraft,
  positionOptions,
  replaceStep,
  rootOptions,
  setRoot,
  shapeText,
  toPath,
} from './paths/pathBuilder'
export type { ChildOption, DraftProblem, DraftStep, DraftView, NextSteps, PathDraft, PositionOption, RootOption } from './paths/pathBuilder'
export { formatPath, parsePath, resolvePath } from './paths/paths'
export type { ObjectPath, ParsedPath, PathStep, Resolution, Target } from './paths/paths'
export { concretePins } from './program/pins'
export type { ConcretePin, ConcretePins } from './program/pins'
export { newBlock } from './program/program'
export type { BlockInstance, FieldValue, Project, Source, Target as OutputTarget } from './program/program'
export {
  addVariable,
  checkVariables,
  findVariable,
  flattenVariables,
  locate,
  moveVariable,
  nameProblem,
  removeVariable,
  renameVariable,
  updateVariable,
  VALUE_TYPES,
} from './variables/variables'
export type { FolderVariable, Location, ReferenceVariable, ValueType, ValueVariable, Variable, VariableProblem } from './variables/variables'
