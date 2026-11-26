/**
 * @file
 * @brief Kontrakt wyświetlacza (sys_display_contract): sprawdza argumenty niezależne od urządzenia (NULL,
 *        zakresy) i przekazuje operacje do sys_device_read / sys_device_write. Proste odczyty
 *        (GEOMETRY, HEALTH) idą makrem SYS_DISPLAY_READ. API i błędy: sys_display_contract.h.
 */
#include "sys_display_contract.h"

ERR_MODULE_DEFINE(MODULE_ID_DISPLAY, ERR_SYS_DISPLAY_MAP)

err_h sys_display_set_backlight(uint8_t device, uint16_t brightness_duty)
{
    ERR_CHECK_RANGE(brightness_duty, 0, SYS_DISPLAY_BRIGHTNESS_MAX);
    return SYS_DISPLAY_WRITE(device, OP_BACKLIGHT, .brightness_duty = brightness_duty);
}

err_h sys_display_get_backlight(uint8_t device, uint16_t *out)
{
    ERR_CHECK(out != NULL, ERR_BAD_ARG);
    display_backlight_args_t a;
    ERR_TRY(SYS_DISPLAY_READ(device, OP_BACKLIGHT, &a));
    *out = a.brightness_duty;
    return NULL;
}

err_h sys_display_set_orientation(uint8_t device, uint8_t orientation)
{
    ERR_CHECK_RANGE(orientation, SYS_DISPLAY_ORIENTATION_0, SYS_DISPLAY_ORIENTATION_270);
    return SYS_DISPLAY_WRITE(device, OP_ORIENTATION, .orientation = orientation);
}

err_h sys_display_get_orientation(uint8_t device, uint8_t *out)
{
    ERR_CHECK(out != NULL, ERR_BAD_ARG);
    display_orientation_args_t a;
    ERR_TRY(SYS_DISPLAY_READ(device, OP_ORIENTATION, &a));
    *out = a.orientation;
    return NULL;
}

err_h sys_display_flush(uint8_t device, const void *pixels, uint32_t len,
                        const display_window_t *window, display_flush_done_f done, void *done_ctx)
{
    ERR_CHECK(window != NULL && done != NULL, ERR_BAD_ARG);
    return SYS_DISPLAY_WRITE(device, OP_FLUSH, .pixels = pixels, .len = len, .window = *window,
                             .done = done, .done_ctx = done_ctx);
}
