# Device-definition packages

A device definition describes a **type of device**, not a board installation or its current live state. It is declarative data that lets the project compiler and UI build install forms, contract controls, documentation and diagnostics from the same source.

The package owns identity, assets, presentation metadata, device-intrinsic capabilities, the decoder packet used to install it, and mappings to generic firmware contracts. A board profile supplies real buses, pins and topology. A runtime twin supplies discovered capabilities, live values and availability. The UI intersects those three layers; firmware remains the final authority for validation.

Contract methods reference the existing decoder and firmware API rather than duplicate their wire layout. Each method can declare typed parameters, static constraints and a runtime capability source. Use `null` for a limit that is intentionally discovered at runtime, never an invented maximum.

Device packages may include asset references, UI labels, tooltips and tab placement. They can add contextual recovery guidance for a firmware error, but the canonical error codes and behavior remain in firmware. Initially packages are data only: arbitrary JavaScript is not loaded from a package.
