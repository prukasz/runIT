#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "sys_device.h"
#include "sys_screen.h"

#ifdef __cplusplus
extern "C" {
#endif


#define VM_SCREEN_OP_BIND_VALUE 0x01u  // vm_screen_value_bind_t: an object shown in a widget
#define VM_SCREEN_OP_BIND_EVENT 0x02u  // vm_screen_event_bind_t: what an event of a widget does
#define VM_SCREEN_OP_CLEAR      0x03u  // write only, no args: drops every binding (the widgets stay)
#define VM_SCREEN_OP_STATUS     0x04u  // read only: vm_screen_status_t

#define VM_SCREEN_SUFFIX_MAX 8u  // the unit written after a number in a text, e.g. " mA" (NUL padded)

/* How the value of an object is put in the widget. */
typedef enum vm_screen_format_e {
  VM_SCREEN_FORMAT_NUMBER = 0,      // a number property of the widget (VALUE, MIN, MAX...): sys_screen_set_int
  VM_SCREEN_FORMAT_APPEND = 1,      // a point at the end of a chart: sys_screen_append (prop is ignored)
  VM_SCREEN_FORMAT_TEXT_INT = 2,    // TEXT: "123" + suffix
  VM_SCREEN_FORMAT_TEXT_FIXED1 = 3, // TEXT: the object / 10 with one decimal, "12.3" + suffix
  VM_SCREEN_FORMAT_TEXT_FIXED2 = 4, // TEXT: the object / 100 with two decimals, "1.23" + suffix
  VM_SCREEN_FORMAT_TEXT_STRING = 5, // TEXT: the object holds a text, shown as it is (suffix ignored)
} vm_screen_format_e;

/* What an event binding does when its widget reports the event. */
typedef enum vm_screen_action_e {
  VM_SCREEN_ACTION_WRITE = 0,  // the number or the text of the event is written to the VM object `target`
  VM_SCREEN_ACTION_RAISE = 1,  // the VM event `target` is raised for the program (a button press)
  VM_SCREEN_ACTION_SHOW = 2,   // the SCREEN widget `target` is shown
} vm_screen_action_e;

/* BIND_VALUE: a row is added; the same (object, widget, prop) twice replaces it. */
typedef struct __attribute__((packed)) {
  uint16_t object;          // the VM object (its id in the program: to check against vm_core)
  uint16_t widget;          // the app's id in sys_screen
  uint8_t prop;             // sys_screen_prop_e: where the value goes (TEXT for the text formats)
  uint8_t format;           // vm_screen_format_e
  uint16_t min_period_ms;   // the least time between two updates of this widget (a chart: its sampling period)
  char suffix[VM_SCREEN_SUFFIX_MAX];  // the unit after a number in a text
} vm_screen_value_bind_t;

/* BIND_EVENT: a row is added; the same (widget, event) twice replaces it. The adapter switches the event on in sys_screen
 * itself. */
typedef struct __attribute__((packed)) {
  uint16_t widget;  // the app's id in sys_screen
  uint8_t event;    // sys_screen_event_e
  uint8_t action;   // vm_screen_action_e
  uint16_t target;  // WRITE: a VM object, RAISE: a VM event, SHOW: a SCREEN widget
} vm_screen_event_bind_t;

typedef struct __attribute__((packed)) {
  uint16_t value_binds;      // BIND_VALUE rows now
  uint16_t value_binds_max;  // what the static table holds (CONFIG_VM_SCREEN_VALUE_BINDS_MAX)
  uint16_t event_binds;      // BIND_EVENT rows now
  uint16_t event_binds_max;  // CONFIG_VM_SCREEN_EVENT_BINDS_MAX
  uint32_t updates;          // widget updates done
  uint32_t actions;          // event actions done
  uint32_t errors;           // bindings that failed (the errors went to the error chain)
} vm_screen_status_t;

/* Errors of the module (ERR_VM_SCREEN_*): FULL (a table), NO_OBJECT (the program has none such), BAD_FORMAT (a format the
 * property cannot take), BAD_ACTION. A binding to a widget that is gone is counted in `errors` and skipped: the rest of
 * the pass still runs. */

/* ----------------------------------------------------------------------------
 * C API (no wire)
 * ------------------------------------------------------------------------- */

/** @brief Boot step, after the VM and sys_screen: empty tables and the adapter's task. */
MUST_USE err_h vm_screen_init(void);

/** @brief The ops as functions: the router's ops and the program loader call these. */
MUST_USE err_h vm_screen_bind_value(const vm_screen_value_bind_t* bind);
MUST_USE err_h vm_screen_bind_event(const vm_screen_event_bind_t* bind);
MUST_USE err_h vm_screen_clear(void);
MUST_USE err_h vm_screen_get_status(vm_screen_status_t* out);

/**
 * @brief The VM calls it at the end of every pass (set as a hook of the supervisor): it only wakes the adapter's task
 * and returns, so the pass is not slowed by the screen. Never blocking.
 */
void vm_screen_pass_done(void);

/**
 * @brief The program lifecycle: a loaded or reset program has a new screen, so the tables and the widgets are dropped
 * (vm_screen_clear and sys_screen_clear); stopped, the adapter does not touch the screen.
 */
void vm_screen_program_reset(void);
void vm_screen_program_running(bool running);

#ifdef __cplusplus
}
#endif
