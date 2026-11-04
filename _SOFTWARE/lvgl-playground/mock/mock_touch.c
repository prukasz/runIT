#include "mock_touch.h"

static int tx, ty;
static bool down;

void mock_touch_set(int x, int y, bool pressed)
{
    tx = x;
    ty = y;
    down = pressed;
}

bool mock_touch_get(int *x, int *y)
{
    *x = tx;
    *y = ty;
    return down;
}
