#include <math.h>
#include "vm_block_servo.h"

bool vm_verify_servo(vm_block_h b) {
  vm_block_servo_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  if (data.when_not_active > VM_SERVO_IDLE_RELEASE) return false;
  return isfinite(data.value);
}

// One operation of the servo device through the router: [op][device_id][args].
static err_h servo_call(uint8_t device_id, uint8_t op, const void* args, size_t args_len) {
  uint8_t frame[2 + sizeof(servo_set_t)] = {op, device_id};
  if (args_len) memcpy(frame + 2, args, args_len);
  uint8_t reply_buf[1];
  sys_device_reply_t reply = {.buf = reply_buf, .cap = sizeof(reply_buf), .len = 0};
  return sys_device_route(frame, 2 + args_len, &reply);
}

// What the servo does while it is not following the value (HOLD sends nothing).
static void servo_idle(vm_block_h b, vm_block_servo_data_t* d) {
  if (d->when_not_active == VM_SERVO_IDLE_DEFAULT && d->last != VM_SERVO_LAST_DEFAULT) {
    BLOCK_CALL(servo_call(d->device_id, SERVO_OP_HOME, NULL, 0), b);
    if (likely(!vm_block_failed(b))) d->last = VM_SERVO_LAST_DEFAULT;
  } else if (d->when_not_active == VM_SERVO_IDLE_RELEASE && d->last != VM_SERVO_LAST_RELEASE) {
    BLOCK_CALL(servo_call(d->device_id, SERVO_OP_RELEASE, NULL, 0), b);
    if (likely(!vm_block_failed(b))) d->last = VM_SERVO_LAST_RELEASE;
  }
}

void vm_blk_servo(vm_block_h b) {
  vm_block_servo_data_t* d = (vm_block_servo_data_t*)vm_block_get_custom_data(b);

  IF_BLOCK_ENABLED(b) {
    float value = 0;
    err_h err = VM_BLOCK_GET_PARAM(value, b, VM_IN_SERVO_VALUE, d->value);
    if (unlikely(!vm_block_check(b, err))) {
      vm_block_set_eno(b, false);
      return;
    }
    // Tenths on the wire; the device range-checks, so saturating here only keeps the cast defined.
    float x10 = isfinite(value) ? roundf(value * 10.0f) : 0.0f;
    if (x10 > INT16_MAX) x10 = INT16_MAX;
    if (x10 < INT16_MIN) x10 = INT16_MIN;
    int16_t value_x10 = (int16_t)x10;

    if (d->last != VM_SERVO_LAST_VALUE || value_x10 != d->last_x10) {
      servo_set_t args = {.value_x10 = value_x10};
      BLOCK_CALL(servo_call(d->device_id, SERVO_OP_SET, &args, sizeof(args)), b);
      if (unlikely(vm_block_failed(b))) {
        vm_block_set_eno(b, false);
        return;
      }
      d->last = VM_SERVO_LAST_VALUE;
      d->last_x10 = value_x10;
    }
    vm_block_set_eno(b, true);
    return;
  }

  servo_idle(b, d);
  vm_block_set_eno(b, false);
}
