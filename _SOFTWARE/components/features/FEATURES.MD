# Features Subsystem

Features are higher-order functional entities and domain abstractions (actuators, sensors, or composite virtual items) built on top of the `sys` and `device` layers. They are **not** tied to a single silicon model or IC.

---

## 1. Architecture & Concept

- **Hardware Agnostic**: Features interact with hardware via `sys_io` and `sys_device` APIs (`sys_io_set_pwm_duty`, `sys_io_set_level`, `sys_io_read_adc_mv`), targeting a `(device_id, pin_num)` pair. The backing device can be any peripheral implementing the required contract (e.g. `PCA9685`, native ESP32 GPIO/LEDC).
- **Physical & Engineering Units**: Rather than raw duty cycles or pin voltages, features accept domain parameters: degrees ($^\circ$), speed ($-1.0\dots+1.0$), current ($\text{mA}$), temperature ($^\circ\text{C}$), colors (RGB/HSV), etc.
- **Unified Registry**: Every feature is managed dynamically by the `feature_registry` linked list, identified by a unique `feature_id`.
- **Automatic Lifecycle & Teardown**: Features provide a teardown callback (e.g. shutting off PWM duty to 0) executed automatically when `feature_remove(id)` or `feature_remove_all()` is called.

---

## 2. Directory Structure

```
components/features/
├── CMakeLists.txt              # Single component collecting subfolders
├── FEATURES.MD                 # This documentation
├── include/
│   └── features.h              # Aggregator header for all features
├── registry/
│   ├── include/
│   │   └── feature_registry.h  # Global linked-list registry declarations
│   └── features_registry.c     # Registry implementation (LL_ macros, mutex-protected)
├── servo/
│   ├── include/
│   │   └── feature_servo.h     # Servo feature declarations & feature_servo_t struct
│   └── feature_servo.c         # Servo feature math, trim, homing, and sys_io dispatch
└── hbridge/
    ├── include/
    │   └── feature_hbridge.h   # H-Bridge / DC motor feature declarations & struct
    └── feature_hbridge.c       # Slow/fast decay math, current sense (IPROPI), VREF limiting
```

---

## 3. Global Registry API (`feature_registry.h`)

The registry manages dynamic feature allocations using `utils.h` linked-list macros (`LL_APPEND`, `LL_DELETE`, `LL_FOREACH`, `LL_FOREACH_SAFE`):

- **`feature_alloc(id, bytes, teardown, out_handle)`**: Allocates the node and feature struct, verifies ID uniqueness, registers it into the list, and returns the pointer.
- **`feature_get_by_id(id)`**: Fast traversal returning the feature's typed data pointer.
- **`feature_remove(id)`**: Calls feature teardown, unlinks from the list, and frees memory.
- **`feature_remove_all()`**: Sweeps and deallocates all features safely.

---

## 4. Servo Feature (`feature_servo.h`)

### Data Structure (`feature_servo_t`)
Configuration fields are placed on top, followed by runtime state:

```c
typedef struct {
  /* --- Configuration (Top) --- */
  uint8_t device_id;             /* Underlying device providing PWM (e.g. PCA9685) */
  sys_io_pin_num_t pin_num;      /* Pin / channel number on that device */
  uint16_t frequency_Hz;         /* PWM frequency, typically 50 Hz */
  uint16_t pulse_min_us;         /* Pulse width for angle_min (e.g. 500 us) */
  uint16_t pulse_max_us;         /* Pulse width for angle_max (e.g. 2500 us) */
  float angle_min;               /* Minimum logical angle (e.g. 0.0f or -90.0f) */
  float angle_max;               /* Maximum logical angle (e.g. 180.0f or +90.0f) */
  float default_angle;           /* Home/boot position in degrees */
  float trim_angle;              /* Calibration offset in degrees */
  bool inverted;                 /* Invert direction */

  /* --- Runtime State (Below) --- */
  float last_angle;              /* Last commanded logical angle */
  bool is_attached;              /* Active PWM output flag */
} feature_servo_t;
```

### Key Functions
- **`feature_servo_create(id, config)`**: Registers the servo, applies defaults (50 Hz, 500–2500 $\mu\text{s}$), initializes hardware frequency, and positions the servo at `default_angle`.
- **`feature_servo_set_angle(id, angle)`**: Calculates effective angle with `trim_angle`, clamps within `[angle_min, angle_max]`, converts $\mu\text{s} \rightarrow \text{PWM duty}$, and dispatches to `sys_io_set_pwm_duty`.
- **`feature_servo_go_home(id)`**: Immediately repositions servo to `default_angle`.
- **`feature_servo_set_trim(id, trim_angle)`**: Updates sub-degree offset and recalculates current position live.
- **`feature_servo_attach(id)` / `feature_servo_detach(id)`**: Enables or disables active PWM generation (stopping holding torque).

---

## 5. Double-Tier H-Bridge Architecture

The H-Bridge subsystem is structured as a **Double-Tier Architecture**, cleanly separating silicon driver concerns from domain actuator abstractions:

```
┌────────────────────────────────────────────────────────┐
│             Features Layer (Generic, Domain)           │
│  feature_servo              feature_hbridge (Motor)    │
│                             - bridge_device_id         │
│                             - channel (0 or 1)         │
│                             - speed (-1.0f .. +1.0f)   │
└───────────────────────────┬────────────────────────────┘
                            │ SYS_DEVICE_CONTRACT_HBRIDGE
┌───────────────────────────┴────────────────────────────┐
│              Devices Layer (Silicon-Specific)          │
│  device_pca9685     device_tca6424a    device_drv8962  │
│  (PWM expander)     (GPIO expander)    (4-CH Driver)   │
└────────────────────────────────────────────────────────┘
```

### Tier 1: Hardware Driver (`device_drv8962`)
- Implements `SYS_DEVICE_CONTRACT_HBRIDGE` as a first-class `sys_device`.
- Instantiated once per physical chip, managing:
  - 4 Half-Bridges (`OUT1`..`OUT4`) and their PWM/Enable pins.
  - 4 $I_{\text{PROPI}}$ current lines ($212\,\mu\text{A}/\text{A}$ mirror math with $R_{\text{IPROPI}}$).
  - 1 shared `nFAULT` pin interrupt with automatic channel arbitration.
  - 1 shared `nSLEEP` pin with $30\,\mu\text{s}$ reset pulse generation.
  - 1 shared `VREF` DAC pin for current regulation chopping.
  - Configurable `turn_off_at_ocp` safety shutdown.

### Tier 2: Unified Motor Feature (`feature_hbridge.h`)
- Pure, lightweight domain actuator (~70 lines of code).
- Binds to any device providing `SYS_DEVICE_CONTRACT_HBRIDGE` via `(bridge_device_id, channel)`.
- Supports speed normalization (`-1.0f` to `+1.0f`), braking and coasting. It listens inline to its bridge channel's faults and republishes them as `SYS_EVENT_DOMAIN_FEATURE` events (device = feature ID, event = `sys_hbridge_fault_reason_e`, value = mA), see [[SYS_EVENT.MD]].

#### Data Structure (`feature_hbridge_t`)
```c
typedef struct {
  /* --- Configuration (Top) --- */
  uint8_t bridge_device_id;            /* Device ID implementing SYS_DEVICE_CONTRACT_HBRIDGE */
  uint8_t channel;                     /* Channel index (e.g. 0 = Motor A, 1 = Motor B) */
  bool inverted;                       /* Invert direction */

  /* --- Runtime State (Below) --- */
  uint8_t feature_id;                  /* ID assigned to this feature */
  uint8_t fault_sub;                   /* sys_event subscription on the bridge channel */
  bool fault_subscribed;               /* fault_sub is valid */
  float current_speed;                 /* Last commanded speed (-1.0f to +1.0f) */
  int32_t last_current_mA;             /* Last sampled current in mA */
  sys_hbridge_fault_reason_e fault_reason; /* Reason for last detected fault */
  bool is_braking;                     /* True if in active brake state */
  bool is_coasting;                    /* True if in Hi-Z coast state */
  bool is_fault;                       /* True if fault was detected */
} feature_hbridge_t;
```

#### Key Functions
- **`feature_hbridge_create(id, config)`**: Binds to `sys_hbridge` channel, sets Full-Bridge mode, configures fault routing, and sets motor to brake.
- **`feature_hbridge_set_speed(id, speed)`**: Normalizes and passes through to `sys_hbridge_set_drive()`.
- **`feature_hbridge_brake(id)`**: Actively shorts motor terminals together via `sys_hbridge_brake()`.
- **`feature_hbridge_coast(id)`**: Disables half-bridges (Hi-Z) via `sys_hbridge_coast()`.
- **`feature_hbridge_get_current_mA(id, out_mA)`**: Reads measured current in mA from the driver.
- **`feature_hbridge_set_current_limit_mA(id, limit_mA)`**: Updates chopping limit on the driver.
- **`feature_hbridge_get_fault(id, out_fault)`**: Checks channel fault state.
- **`feature_hbridge_get_fault_reason(id, out_reason)`**: Retrieves detailed cause (`OVERCURRENT`, `THERMAL_OR_UVLO`, `EXTERNAL`).
- **`feature_hbridge_clear_fault(id)`**: Clears driver fault latch and resets software state.
