/* A fake touch controller: the scenario sets the finger, LVGL's read callback polls it. */
#ifndef MOCK_TOUCH_H
#define MOCK_TOUCH_H

#include <stdbool.h>

void mock_touch_set(int x, int y, bool pressed);
bool mock_touch_get(int *x, int *y);   /* returns pressed */

#endif
