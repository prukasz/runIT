#include "sim_gpio.h"

#include "mock_ili9341.h"
#include "mock_panel.h"
#include "sys_io.h"
#include "utils.h"

typedef struct {
    sys_device_base_t base;  /* first */
} sim_gpio_t;

static err_h check_pin(const sim_gpio_t *g, uint8_t pin)
{
    ERR_CHECK(pin < SIM_GPIO_PINS, ERR_DEV_BAD_PIN, .id = g->base.id, .pin = pin);
    return NULL;
}

static void drive(uint8_t pin, uint32_t duty)
{
    if (pin == SIM_GPIO_PIN_LCD_RESET) {
        mock_ili9341_resx(duty != 0u);
    } else if (pin == SIM_GPIO_PIN_LCD_BL) {
        mock_panel_set_brightness((uint16_t)duty);
    }
}

OP_WRITE(IO, MODE)
{
    (void)out;
    return check_pin(ctx, args->pin);
}

OP_WRITE(IO, LEVEL)
{
    (void)out;
    ERR_TRY(check_pin(ctx, args->pin));
    drive(args->pin, args->level ? IO_PWM_DUTY_MAX : 0u);
    return NULL;
}

OP_WRITE(IO, PWM_DUTY)
{
    (void)out;
    ERR_TRY(check_pin(ctx, args->pin));
    SYS_DEV_CHECK_IN_RANGE(((sim_gpio_t *)ctx)->base.id, args->duty, 0, IO_PWM_DUTY_MAX);
    drive(args->pin, args->duty);
    return NULL;
}

OP_WRITE(IO, RESET)
{
    (void)out;
    ERR_TRY(check_pin(ctx, args->pin));
    drive(args->pin, args->pin == SIM_GPIO_PIN_LCD_RESET ? IO_PWM_DUTY_MAX : 0u);  /* RESX has its pull-up */
    return NULL;
}

static err_h no_op(void *ctx)
{
    (void)ctx;
    return NULL;
}

static err_h create(void *ctx, const void *cfg)
{
    (void)ctx;
    (void)cfg;
    return NULL;
}

static const sys_device_op_t k_ops[] = {
    OP_ENTRY_W(IO, MODE),
    OP_ENTRY_W(IO, LEVEL),
    OP_ENTRY_W(IO, PWM_DUTY),
    OP_ENTRY_W(IO, RESET),
};

static const sys_device_type_t k_type = {
    .type      = SIM_DEVICE_TYPE_GPIO,
    .cfg_size  = 0,
    .ctx_size  = sizeof(sim_gpio_t),
    .create    = create,
    .uninstall = no_op,
    .suspend   = no_op,
    .resume    = no_op,
    .ops       = k_ops,
    .op_count  = ARRAY_LEN(k_ops),
};

SYS_DEVICE_TYPE_REGISTER(k_type)
