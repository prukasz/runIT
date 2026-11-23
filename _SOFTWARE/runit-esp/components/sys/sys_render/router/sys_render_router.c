/**
 * @file
 * @brief Remote interface of sys_render (router/sys_render_router.h): maps RENDER_OPERATIONS onto
 *        sys_render_* and self-registers under MODULE_ID_RENDER.
 */
#include "sys_render/router/sys_render_router.h"

ROUTER_GETTER(RENDER, OP_INSTALL, sys_render_get_install)
ROUTER_SETTER(RENDER, OP_INSTALL, sys_render_install)

ROUTER_WRITE(RENDER, OP_RESET)
{
    return sys_render_reset();
}

ROUTER_READ(RENDER, OP_ROTATION)
{
    sys_render_rotation_t r;
    ERR_TRY(sys_render_get_rotation(&r.rotation));
    return cmd_reply(out, &r, sizeof(r));
}

ROUTER_WRITE(RENDER, OP_ROTATION)
{
    return sys_render_set_rotation(args->rotation);
}

ROUTER_WRITE(RENDER, OP_START)
{
    return sys_render_start();
}

ROUTER_WRITE(RENDER, OP_STOP)
{
    return sys_render_stop();
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(RENDER, OP_INSTALL),  ROUTER_ENTRY_W(RENDER, OP_RESET),
    ROUTER_ENTRY(RENDER, OP_ROTATION), ROUTER_ENTRY_W(RENDER, OP_START),
    ROUTER_ENTRY_W(RENDER, OP_STOP),
};

ROUTER_MODULE(sys_render_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_RENDER, sys_render_router)
