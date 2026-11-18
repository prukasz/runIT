#pragma once

/* The widgets, layouts and themes LVGL has (lv_conf.h). Every one is listed, on or off, so this file is the whole list:
 * a widget that is off is not built (no flash) and the screen layer cannot make it. On: what the screen layer
 * (sys_screen) makes, and what those need (Requires). */

/* Widgets */
#define LV_USE_ARC          1  // a gauge or knob
#define LV_USE_BAR          1  // a value in a range
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1  // Requires of KEYBOARD
#define LV_USE_CHART        1  // a line chart
#define LV_USE_CHECKBOX     1
#define LV_USE_IMAGE        1  // icons
#define LV_USE_KEYBOARD     1  // the on-screen keyboard of a TEXTAREA
#define LV_USE_LABEL        1  // a text
#define LV_USE_LED          1  // a lamp
#define LV_USE_SLIDER       1  // Requires: BAR
#define LV_USE_SPINBOX      1  // a number with plus and minus; Requires: TEXTAREA
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1  // a one-line field; Requires: LABEL

#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CANVAS      0
#define LV_USE_DROPDOWN    0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_LINE        0
#define LV_USE_LIST        0
#define LV_USE_LOTTIE      0
#define LV_USE_MENU        0
#define LV_USE_MSGBOX      0
#define LV_USE_ROLLER      0
#define LV_USE_SCALE       0
#define LV_USE_SPAN        0
#define LV_USE_SPINNER     0
#define LV_USE_TABLE       0
#define LV_USE_TABVIEW     0
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0
#define LV_USE_3DTEXTURE   0

/* Layouts: a container lays its children out in a row or a column (FLEX); no grid. */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Themes: the default one styles every widget; no other. */
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0
