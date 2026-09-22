# Element 06: Telemetry & Action Recorder

## 1. Concept Overview
A time-series telemetry logger and action recorder that captures device states, variable mutations, and packet traffic over time, with the ability to export and replay sequences.

## 2. Key Responsibilities
- **Action Capture**: Records timestamped arrays of state frames:
  - Sent user commands (slider moves, joystick inputs).
  - Received sensor telemetry and peripheral responses.
  - System errors and status flag changes.
- **Ring Buffer Management**: Maintains a lightweight in-memory circular buffer to avoid browser memory leaks during long test runs.
- **Save / Export / Replay**:
  - Export recorded sessions to JSON.
  - Replay mode: step through past frames to debug hardware glitches or test how blocks reacted.

## 3. UI / Layout Requirements
- Top bar quick-record button (REC indicator with pulsing red badge).
- Buffer memory utilization meter.
- Bottom drawer telemetry console tab with filter and export actions.

