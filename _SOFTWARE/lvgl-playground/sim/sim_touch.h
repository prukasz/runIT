/* The simulated touch: a device of contract TOUCH whose finger is mock_touch (the window's mouse, the scenario's
 * taps), in the glass's pixels. CREATE takes the real touch_cfg_t (max_x, max_y and axes are used; the bus is not).
 * Capacitive: no CALIBRATION (has_op leaves it out). */
#pragma once

#define SIM_DEVICE_TYPE_TOUCH 0x7F01u  /* host only: not in sys_device_types.h */
