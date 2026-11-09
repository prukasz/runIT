#include "sim_touch.h"

#include <string.h>

#include "mock_touch.h"
#include "sys_touch_contract.h"
#include "utils.h"

typedef struct {
    sys_device_base_t base;  /* first */
    touch_cfg_t cfg;
    touch_point_t last;
} sim_touch_t;

static uint16_t clamp(int v, uint16_t max)
{
    return v < 0 ? 0 : v >= max ? (uint16_t)(max - 1) : (uint16_t)v;
}

/* The finger, through the axes, clamped to max_x, max_y; not pressed while suspended, the last point kept. */
OP_READ(TOUCH, POINT)
{
    (void)args;
    sim_touch_t *t = ctx;
    int x, y;
    const bool pressed = mock_touch_get(&x, &y) && !t->base.suspended;
    if ((t->cfg.axes & TOUCH_AXES_SWAP_XY) != 0u) {
        const int tmp = x;
        x = y;
        y = tmp;
    }
    if ((t->cfg.axes & TOUCH_AXES_MIRROR_X) != 0u) x = t->cfg.max_x - 1 - x;
    if ((t->cfg.axes & TOUCH_AXES_MIRROR_Y) != 0u) y = t->cfg.max_y - 1 - y;
    if (pressed) {
        t->last.x = clamp(x, t->cfg.max_x);
        t->last.y = clamp(y, t->cfg.max_y);
    }
    t->last.pressed = pressed ? 1u : 0u;
    return cmd_reply(out, &t->last, sizeof(t->last));
}

OP_READ(TOUCH, CALIBRATION)
{
    (void)ctx;
    (void)args;
    (void)out;
    return NULL;  /* never called: has_op leaves it out */
}

OP_WRITE(TOUCH, CALIBRATION)
{
    (void)ctx;
    (void)args;
    (void)out;
    return NULL;
}

static bool has_op(const void *ctx, uint16_t op)
{
    (void)ctx;
    return op != TOUCH_OP_CALIBRATION;
}

OP_WRITE(SYS_DEVICE, RESET)
{
    (void)args;
    (void)out;
    memset(&((sim_touch_t *)ctx)->last, 0, sizeof(touch_point_t));
    return NULL;
}

static err_h suspend_hook(void *ctx)
{
    (void)ctx;
    return NULL;
}

static err_h resume_hook(void *ctx)
{
    (void)ctx;
    return NULL;
}

static err_h uninstall(void *ctx)
{
    (void)ctx;
    return NULL;
}

static err_h create(void *ctx, const void *cfg_in)
{
    sim_touch_t *t = ctx;
    memcpy(&t->cfg, cfg_in, sizeof(t->cfg));
    SYS_DEV_CHECK_IN_RANGE(t->base.id, t->cfg.max_x, 1, 4096);
    SYS_DEV_CHECK_IN_RANGE(t->base.id, t->cfg.max_y, 1, 4096);
    return NULL;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(SYS_DEVICE, RESET),
    OP_ENTRY_R(TOUCH, POINT),
    OP_ENTRY(TOUCH, CALIBRATION),
};

static const sys_device_type_t k_type = {
    .type      = SIM_DEVICE_TYPE_TOUCH,
    .cfg_size  = sizeof(touch_cfg_t),
    .ctx_size  = sizeof(sim_touch_t),
    .create    = create,
    .uninstall = uninstall,
    .suspend   = suspend_hook,
    .resume    = resume_hook,
    .ops       = k_ops,
    .op_count  = ARRAY_LEN(k_ops),
    .has_op    = has_op,
};

SYS_DEVICE_TYPE_REGISTER(k_type)
