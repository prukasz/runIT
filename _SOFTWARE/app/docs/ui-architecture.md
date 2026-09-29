# UI architecture

## Screen frame

`src/App.tsx` coordinates the active view and workspaces. It supplies content to `src/layout/WorkspaceLayout.tsx`, which owns the stable three-column structure:

| Region | Purpose | Typical content |
| --- | --- | --- |
| Explorer | Tree, palette, and lists | Variables, blocks, devices, settings categories |
| Main screen | Canvas or editor | Block canvas, object editor, device editor, settings editor |
| Details | Selection details and actions | Block settings, object information, device commands, run and connection panels |

The main screen receives a toolbar slot from `App.tsx`, so actions can change with the active view without changing the frame. The terminal is below the main screen.

The explorer and details panels share `side-panel`, `side-panel-header`, and `side-panel-collapsed-icons` styles. The three panel resize handles share `panel-resize-handle`; their direction and placement stay in the feature classes.

## Shared controls

| Component | Use |
| --- | --- |
| `components/FormField/TextField` | Native text and number inputs with shared border, focus, disabled, and invalid states. Checkbox, file, and other native controls retain their native appearance. |
| `components/FormField/SelectField` | Native dropdowns with the same field surface and focus behavior. Keep native select semantics for finite choices. |
| `components/EditableField` | A text field with an edit or protected icon. Supports editable, read-only, and disabled values. |
| `components/ListableField` | A dropdown with option data and a chevron or locked icon; built on `SelectField`. |
| `components/PaletteSearch` | Search input, icon, clear action, and Escape behavior for explorer palettes. Views supply their own filtering rules. |
| `components/PaletteSectionHeader` | Collapsible palette heading with chevron, label, filtered count, and optional Add action. Views own expanded state and filtering. |
| `components/InlineRename` | One commit or cancel path for canvas names, block names, and aliases; callers decide how to validate the final text. |
| `components/Badge` | Passive chip for system markers, IDs, counts, BLE flags and directions, detail labels, and type codes. Set a semantic `tone`; use `size="compact"` for dense flags, `size="detail"` in inspectors, `caps` for uppercase status text, and `push` for edge alignment. `filled` is used for prominent accent labels. |
| `components/Button` | `Button` (`variant`: default, primary, danger, dashed; `size`: md 32px, sm 28px, icon, icon-sm; `block` fills the row), `buttonClass()` for a link styled as a button, and `ToggleChip` (pill with `selected`). Features add no button colors, borders, or icon sizes of their own; toolbar and tree icon buttons keep the shell's plain `.shell button` style. |
| `components/Card` | `Card` (title, optional amber `icon`, `subhead`) is the panel surface for a group of settings; `CardStack` stacks the cards of one editor page. |
| `components/FormField/FormRow` | `FormRow` (label above one control, optional `htmlFor` / `note` through `FieldLabel`, `hint` under the control) and `FormGrid` (two columns, one when narrow). |
| `components/PanelHeader` | Title row at the top of a details panel: leading `icon`, `title`, then badges or actions as children. |
| `components/TypeBadge` | Exact VM or BLE type codes and the broader value-kind icon used on objects and block pins. |
| `components/OptionMenu` | Rich option lists with a shared option row, selection state, arrow navigation, and Escape. Canvas wiring and accessor suggestions use it. |
| `components/TreeSlab` | Shared leading badge, label, trailing badges, and optional action row for variables, devices, connectors, and the block palette. |

`TreeSlab.css` owns row selection, folder, search match, link target, drag, and drop states. A feature should add only its own row content or spacing rules.
Use `Badge` for static labels. Buttons and selectable chips keep their own interaction styles and semantics.
Flat palettes can give the row a `listitem` role and disable activation when their item is unavailable. The row's main button supplies Enter and Space activation; drag handlers remain on the row container.

Use a native select for ordinary finite choices. Use `OptionMenu` when a row needs badges, descriptive text, live filtering, or a drop target. Keep the options and their domain rules in the feature; the shared component owns the common interaction and surface.

Use `form-field-panel` on `TextField` or `SelectField` for fields on a panel surface. A compact field can set `--field-height`, `--field-padding`, and `--field-font-size` on its container. The shared control keeps the border, focus, and disabled states.

`EditableField` and `ListableField` share the `adorned-field-wrap` and `field-adornment-*` styles from `FormField.css`. Their own stylesheets contain only control-specific sizing. Native fields supplied through `TextField` and `SelectField` should keep their common border, colors, and focus state in `FormField.css`; feature CSS should only override the needed dimensions or layout.

## Feature boundaries

`src/domain/` contains project data, catalogs, wiring, compilation, and other rules independent of React. `src/devices/` and the workspace modules contain their feature views and state hooks.

The canvas feature is grouped by responsibility:

| Directory | Contents |
| --- | --- |
| `canvas/blocks/` | Block rendering, details, formula editing, pin helpers, and block-specific rules. Accessor parsing lives in `pinAccessors.ts`; `PinHelpers.tsx` uses it. |
| `canvas/surface/` | Pan and zoom interaction, wires, free chips, and branch or loop regions. |
| `canvas/workspace/` | Canvas editor composition, tabs, block palette, and workspace state. |
| `canvas/index.ts` | Public imports for the rest of the app. |

`canvas/Canvas.css` remains the feature stylesheet and is imported by the feature views.

## File view

`ProjectFile.tsx` is the File view (first icon in the left rail): the left panel holds New, Open, Save (project file), Import and Export (code file, `runit-code`) and Recover from board; the main screen shows what the project holds and the code it builds. The Run view keeps only what changes a board: Store, Load, Erase. `useProjectCode` builds the code once for both.

## Names shown to users

Block enable pins keep their PLC names in code, data files and paths (`EN`, `ENO`, `enables`, `<id>:eno`), but the screen says what they do: **EN** is **Run when** (the block runs only while it is true), **ENO** is **When done** (true after the block ran), and a FOR's ENO is **Loop body**. Use these in labels, titles, hints and messages.

## Color palette

`src/styles/theme.css` is the source of dark and light theme colors. The shell owns surface tokens such as `--background`, `--panel`, `--divider`, `--active`, `--muted`, and the blue selection token `--accent`. Use `--color-*` for status and accent hues, `--type-*` for badges, `--category-*` for block categories, `--group-tint-*` for canvas contours, and `--syntax-*` for expression text. Mix a token with `transparent` for translucent backgrounds and glows so both themes use the same hue. Keep literal colors only for fixed contrast marks or artwork.

`src/hooks/useStorage.ts` owns browser storage: `readStored` / `usePersistEffect` for workspace data, `usePersistedChoice` / `usePersistedFlag` for remembered preferences (tab, mode, theme, snap). Every read and write is guarded, so a view never repeats the try/catch. `src/hooks/useUndoHistory.ts` owns the common undo transitions and 50-entry limit. Object edits can group rapid changes with a key; project loads in canvas, devices, and objects are undoable, while loading a BLE profile starts a fresh history. Each workspace keeps its own storage, validation, and selection behavior.

When adding a screen, provide its explorer, main content, details, and toolbar actions through `App.tsx`. Reuse the shared controls before adding feature-specific field or dropdown styling. Feature CSS should describe layout and meaning, such as the importance level color; control focus and disabled behavior belong to the shared component.
