#pragma once

#include "stdint.h"
#include "stdbool.h"
#include "sys_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The screen composition (sys_screen), layer 4: a screen is a list of commands, like a VM program. The app sends
 * them one by one (drawing live) or as one stream (a stored project), the loader checks each against the dictionary
 * below and builds the LVGL widget tree. It sits on sys_lvgl (layer 3), takes its lock for every command, and knows no
 * lv_* in its header. The binding of a VM object to a widget and what a widget event does is layer 5 (the VM adapter).
 *
 * The dictionary is static (widget types, properties, events with fixed numbers), the screen dynamic. Ids of widgets
 * are the app's (u16); the board keeps a STATIC table of them (CONFIG_SYS_SCREEN_WIDGETS_MAX slots, 64 by default, in
 * the module's own RAM, not in the heap of INSTALL; a full table is ERR_SCREEN_FULL). The widgets themselves live in
 * that heap.
 *
 * Commands under its own ROUTER_KEY (the next free one in router_ids.h). Never renumber. Every frame is a storable
 * record, so a project replays them after INSTALL.
 */
#define SYS_SCREEN_OP_CREATE 0x01u  // sys_screen_create_t
#define SYS_SCREEN_OP_DELETE 0x02u  // sys_screen_delete_t: the widget and all it holds
#define SYS_SCREEN_OP_SET    0x03u  // sys_screen_set_head_t + the value (its type is the property's)
#define SYS_SCREEN_OP_SHOW   0x04u  // sys_screen_show_t: a SCREEN widget becomes the one displayed
#define SYS_SCREEN_OP_EVENT  0x05u  // sys_screen_event_sub_t: which events of a widget are reported
#define SYS_SCREEN_OP_CLEAR  0x06u  // write only, no args: deletes every widget
#define SYS_SCREEN_OP_STATUS 0x07u  // read only: sys_screen_status_t
#define SYS_SCREEN_OP_APPEND 0x08u  // sys_screen_append_t: a new point at the end of a chart

#define SYS_SCREEN_ID_NONE  0xFFFFu  // no widget: the parent of a SCREEN
#define SYS_SCREEN_TEXT_MAX 48u      // a TEXT value, in bytes (fits a remote call with the head)

/* ----------------------------------------------------------------------------
 * The dictionary
 * ------------------------------------------------------------------------- */

typedef enum sys_screen_widget_e {
  SYS_SCREEN_WIDGET_SCREEN = 0,     // a whole screen: no parent, shown by SHOW
  SYS_SCREEN_WIDGET_CONTAINER = 1,  // an empty box that holds other widgets (and lays them out: FLEX_FLOW)
  SYS_SCREEN_WIDGET_LABEL = 2,      // a text
  SYS_SCREEN_WIDGET_BUTTON = 3,     // a button, its text is a label inside
  SYS_SCREEN_WIDGET_BAR = 4,        // a horizontal bar showing a value in a range
  SYS_SCREEN_WIDGET_SLIDER = 5,     // a bar the finger moves
  SYS_SCREEN_WIDGET_SWITCH = 6,     // an on / off switch
  SYS_SCREEN_WIDGET_CHECKBOX = 7,   // a box with a text, on / off
  SYS_SCREEN_WIDGET_ARC = 8,        // a round gauge or knob showing a value in a range
  SYS_SCREEN_WIDGET_LED = 9,        // a lamp: a colored dot, on / off or dimmed
  SYS_SCREEN_WIDGET_CHART = 10,     // a line chart of the last POINTS values, fed by APPEND
  SYS_SCREEN_WIDGET_SPINBOX = 11,   // a number the user changes with a plus and a minus, no keyboard
  SYS_SCREEN_WIDGET_TEXTAREA = 12,  // a one-line field the user types in; Enter reports SUBMITTED with the text
  SYS_SCREEN_WIDGET_KEYBOARD = 13,  // the on-screen keyboard of a TEXTAREA (TARGET): letters, or only digits
} sys_screen_widget_e;

/* How the bytes of a SET value are read (little endian). */
typedef enum sys_screen_value_e {
  SYS_SCREEN_VALUE_I16 = 0,     // signed, 2 bytes
  SYS_SCREEN_VALUE_I32 = 1,     // signed, 4 bytes
  SYS_SCREEN_VALUE_BOOL = 2,    // 1 byte, 0 or 1
  SYS_SCREEN_VALUE_ENUM = 3,    // 1 byte, a member of the property's enum
  SYS_SCREEN_VALUE_COLOR = 4,   // 4 bytes, 0x00RRGGBB
  SYS_SCREEN_VALUE_STRING = 5,  // the rest of the frame, UTF-8, at most SYS_SCREEN_TEXT_MAX bytes
  SYS_SCREEN_VALUE_ID = 6,      // 2 bytes, the id of a widget
} sys_screen_value_e;

/* Each property: its value type, then the widgets that take it (a SET on another widget is refused). */
typedef enum sys_screen_prop_e {
  SYS_SCREEN_PROP_X = 0x01,           // i16, pixels from the parent's alignment point; all but Screen
  SYS_SCREEN_PROP_Y = 0x02,           // i16; all but Screen
  SYS_SCREEN_PROP_WIDTH = 0x03,       // i16, pixels; all but Screen
  SYS_SCREEN_PROP_HEIGHT = 0x04,      // i16, pixels; all but Screen
  SYS_SCREEN_PROP_ALIGN = 0x05,       // enum sys_screen_align_e, where in the parent X and Y count from; all but Screen
  SYS_SCREEN_PROP_HIDDEN = 0x06,      // bool; all but Screen
  SYS_SCREEN_PROP_BG_COLOR = 0x07,    // color; Screen, Container, Button, Bar, Slider, Arc, Led (the lamp's color), Chart
  SYS_SCREEN_PROP_PAD = 0x08,         // i16, pixels inside the edge and between children; Container
  SYS_SCREEN_PROP_FLEX_FLOW = 0x09,   // enum sys_screen_flow_e, lays the children out in a row or a column; Container
  SYS_SCREEN_PROP_TEXT = 0x10,        // string; Label, Button, Checkbox, Textarea (what is in the field)
  SYS_SCREEN_PROP_TEXT_COLOR = 0x11,  // color; Label, Button, Checkbox, Textarea
  SYS_SCREEN_PROP_FONT = 0x12,        // enum sys_screen_font_e; Label, Button, Checkbox, Textarea
  SYS_SCREEN_PROP_VALUE = 0x20,       // i32; Bar, Slider, Arc, Spinbox, Switch and Checkbox (0 or 1), Led (0..255)
  SYS_SCREEN_PROP_MIN = 0x21,         // i32; Bar, Slider, Arc, Spinbox, Chart (the lowest value of the axis)
  SYS_SCREEN_PROP_MAX = 0x22,         // i32; Bar, Slider, Arc, Spinbox, Chart (the highest value of the axis)
  SYS_SCREEN_PROP_STEP = 0x23,        // i32, what one press of plus or minus adds; Spinbox
  SYS_SCREEN_PROP_POINTS = 0x30,      // i16, how many values the chart keeps; Chart (the memory is the heap's)
  SYS_SCREEN_PROP_TARGET = 0x40,      // id, the TEXTAREA this keyboard types into; Keyboard
  SYS_SCREEN_PROP_KEYS = 0x41,        // enum sys_screen_keys_e, letters or only digits; Keyboard
} sys_screen_prop_e;

typedef enum sys_screen_align_e {
  SYS_SCREEN_ALIGN_TOP_LEFT = 0,
  SYS_SCREEN_ALIGN_TOP_MID = 1,
  SYS_SCREEN_ALIGN_TOP_RIGHT = 2,
  SYS_SCREEN_ALIGN_LEFT_MID = 3,
  SYS_SCREEN_ALIGN_CENTER = 4,
  SYS_SCREEN_ALIGN_RIGHT_MID = 5,
  SYS_SCREEN_ALIGN_BOTTOM_LEFT = 6,
  SYS_SCREEN_ALIGN_BOTTOM_MID = 7,
  SYS_SCREEN_ALIGN_BOTTOM_RIGHT = 8,
} sys_screen_align_e;

typedef enum sys_screen_flow_e {
  SYS_SCREEN_FLOW_NONE = 0,    // the children are placed by their own X and Y
  SYS_SCREEN_FLOW_ROW = 1,     // side by side
  SYS_SCREEN_FLOW_COLUMN = 2,  // one under another
} sys_screen_flow_e;

typedef enum sys_screen_keys_e {
  SYS_SCREEN_KEYS_TEXT = 0,    // letters, digits and signs
  SYS_SCREEN_KEYS_NUMBER = 1,  // only digits, minus and point; SUBMITTED then reports the number as `value` too
} sys_screen_keys_e;

typedef enum sys_screen_font_e {
  SYS_SCREEN_FONT_12 = 0,
  SYS_SCREEN_FONT_14 = 1,
  SYS_SCREEN_FONT_16 = 2,
  SYS_SCREEN_FONT_24 = 3,
} sys_screen_font_e;

/* Widget events: each is the LVGL event it is named after. A widget reports only the ones EVENT switched on. */
typedef enum sys_screen_event_e {
  SYS_SCREEN_EVENT_PRESSED = 0,        // LV_EVENT_PRESSED
  SYS_SCREEN_EVENT_RELEASED = 1,       // LV_EVENT_RELEASED
  SYS_SCREEN_EVENT_CLICKED = 2,        // LV_EVENT_CLICKED
  SYS_SCREEN_EVENT_LONG_PRESSED = 3,   // LV_EVENT_LONG_PRESSED
  SYS_SCREEN_EVENT_VALUE_CHANGED = 4,  // LV_EVENT_VALUE_CHANGED: a slider moved, a switch flipped, a spinbox stepped; the value is reported
  SYS_SCREEN_EVENT_SUBMITTED = 5,      // LV_EVENT_READY: Enter or OK in a textarea; the text (and a number, with number keys) is reported
} sys_screen_event_e;

/* ----------------------------------------------------------------------------
 * Command arguments (packed)
 * ------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
  uint16_t id;      // the app's id of the new widget; taken: ERR_SCREEN_ID_TAKEN
  uint16_t parent;  // an existing widget, or SYS_SCREEN_ID_NONE for a SCREEN (and only for it)
  uint8_t type;     // sys_screen_widget_e
} sys_screen_create_t;

typedef struct __attribute__((packed)) {
  uint16_t id;
} sys_screen_delete_t;

/* SET: this head, then the value in the bytes that follow (the type of the property, see the dictionary). */
typedef struct __attribute__((packed)) {
  uint16_t id;
  uint8_t prop;  // sys_screen_prop_e
} sys_screen_set_head_t;

typedef struct __attribute__((packed)) {
  uint16_t id;  // a SCREEN widget
} sys_screen_show_t;

typedef struct __attribute__((packed)) {
  uint16_t id;
  uint8_t event;  // sys_screen_event_e
  uint8_t on;     // 1: report it, 0: stop
} sys_screen_event_sub_t;

/* APPEND: a point at the right end of a chart; the oldest one goes when the chart is full. */
typedef struct __attribute__((packed)) {
  uint16_t id;     // a CHART widget
  int32_t value;
} sys_screen_append_t;

typedef struct __attribute__((packed)) {
  uint16_t widgets;      // widgets now
  uint16_t widgets_max;  // what the table of ids holds
  uint32_t events;       // widget events put in the queue
  uint32_t events_dropped;  // the oldest events thrown out because the queue was full
  uint32_t errors;       // commands refused (the errors went to the error chain)
} sys_screen_status_t;

/* Errors of the module (ERR_SCREEN_*, errors_screen.json): BAD_TYPE, BAD_PROP (unknown, or not for that widget), BAD_VALUE
 * (wrong size, out of the enum, a text too long), NO_WIDGET, NO_PARENT, ID_TAKEN, FULL (the table), NO_MEM (the heap). Each
 * names the widget id; a refused command changes nothing. */

/* ----------------------------------------------------------------------------
 * Widget events: the queue
 *
 * A widget event is made in the LVGL task, under the LVGL lock, by the callback LVGL calls. It copies the event into
 * a FreeRTOS queue (static, CONFIG_SYS_SCREEN_EVENT_QUEUE items, 16 by default) with a zero wait and returns: it never
 * blocks. The consumer (the VM adapter, layer 5) takes them with sys_screen_event_receive in its own task. A full
 * queue throws out the OLDEST event (the newest is the one that counts: the last position of a slider) and counts it
 * in events_dropped. The item is a copy, so the consumer owns what it receives.
 * ------------------------------------------------------------------------- */

#define SYS_SCREEN_EVENT_VALUE_MAX 24u  // the most bytes of an event's value: a text is at most 23 characters and a NUL

//what the value of the event is
typedef enum sys_screen_payload_e {
  SYS_SCREEN_PAYLOAD_NONE = 0,    // no value: PRESSED, RELEASED, CLICKED, LONG_PRESSED
  SYS_SCREEN_PAYLOAD_NUMBER = 1,  // value.number: VALUE_CHANGED, SUBMITTED of a keyboard with number keys
  SYS_SCREEN_PAYLOAD_TEXT = 2,    // value.text: SUBMITTED of a textarea; NUL ended, cut at a character boundary if longer
} sys_screen_payload_e;

//one item of the queue: 28 bytes
typedef struct {
  uint16_t id;       // the widget
  uint8_t event;     // sys_screen_event_e
  uint8_t payload;   // sys_screen_payload_e: which member of value is valid
  union {
    int32_t number;                          // a slider's position, a switch 0 or 1, a typed number
    char text[SYS_SCREEN_EVENT_VALUE_MAX];   // what the user typed
  } value;
} sys_screen_event_t;

/* ----------------------------------------------------------------------------
 * C API (no wire)
 * ------------------------------------------------------------------------- */

/** @brief Boot step, after sys_lvgl_init: an empty table of widgets. */
MUST_USE err_h sys_screen_init(void);

/**
 * @brief Takes the oldest widget event out of the queue, into @p out. Waits at most @p timeout_ms (0: do not wait).
 * @return OK with an event, ERR_SCREEN_NO_EVENT when none came in time (an ordinary answer, not reported). One
 * consumer: the VM adapter.
 */
MUST_USE err_h sys_screen_event_receive(sys_screen_event_t* out, uint32_t timeout_ms);

/** @brief The commands as functions: the router's ops and the VM adapter call these. Each takes the LVGL lock itself
 * (recursive: a caller holding it around a batch, so the screen is not drawn half built, is fine). */
MUST_USE err_h sys_screen_create(uint16_t id, uint16_t parent, sys_screen_widget_e type);
MUST_USE err_h sys_screen_delete(uint16_t id);
MUST_USE err_h sys_screen_set(uint16_t id, sys_screen_prop_e prop, const void* value, uint8_t len);
MUST_USE err_h sys_screen_show(uint16_t id);
MUST_USE err_h sys_screen_enable_event(uint16_t id, sys_screen_event_e event, bool on);
MUST_USE err_h sys_screen_append(uint16_t id, int32_t value);
MUST_USE err_h sys_screen_clear(void);
MUST_USE err_h sys_screen_get_status(sys_screen_status_t* out);

/** @brief Shorthands of SET for the two things the adapter sets all the time: a text (UTF-8, NUL ended) and a number. */
MUST_USE err_h sys_screen_set_text(uint16_t id, const char* text);
MUST_USE err_h sys_screen_set_int(uint16_t id, sys_screen_prop_e prop, int32_t value);

#ifdef __cplusplus
}
#endif
