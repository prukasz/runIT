#include <math.h>
#include "vm_block_hbridge.h"

bool vm_verify_hbridge(vm_block_h b) {
  vm_block_hbridge_data_t data;
  memcpy(&data, vm_block_get_custom_data(b), sizeof(data));
  if (data.action > VM_HBRIDGE_ACTION_COAST) return false;
  if (data.when_not_active > VM_HBRIDGE_IDLE_HOLD) return false;
  return isfinite(data.drive);
}

/* Brake or coast, once: nothing is sent while that is already what the bridge was last told. */
static void hbridge_release(vm_block_h b, vm_block_hbridge_data_t* d, bool brake) {
  const uint8_t wanted = brake ? VM_HBRIDGE_LAST_BRAKE : VM_HBRIDGE_LAST_COAST;
  if (d->last == wanted) return;
  BLOCK_CALL(brake ? sys_hbridge_brake(d->device_id, d->channel) : sys_hbridge_coast(d->device_id, d->channel), b);
  if (likely(!vm_block_failed(b))) d->last = wanted;
}

void vm_blk_hbridge(vm_block_h b) {
  vm_block_hbridge_data_t* d = (vm_block_hbridge_data_t*)vm_block_get_custom_data(b);

  IF_BLOCK_ENABLED(b) {
    if (d->action != VM_HBRIDGE_ACTION_DRIVE) {
      hbridge_release(b, d, d->action == VM_HBRIDGE_ACTION_BRAKE);
      vm_block_set_eno(b, !vm_block_failed(b));
      return;
    }

    float percent = 0;
    err_h err = VM_BLOCK_GET_PARAM(percent, b, VM_IN_HBRIDGE_DRIVE, d->drive);
    if (unlikely(!vm_block_check(b, err))) {
      vm_block_set_eno(b, false);
      return;
    }
    // Tenths of a percent are what changes the command; the device range-checks, so saturating here only keeps the cast defined.
    float x10 = isfinite(percent) ? roundf(percent * 10.0f) : 0.0f;
    if (x10 > INT16_MAX) x10 = INT16_MAX;
    if (x10 < INT16_MIN) x10 = INT16_MIN;
    int16_t drive_x10 = (int16_t)x10;

    if (d->last != VM_HBRIDGE_LAST_DRIVE || drive_x10 != d->last_x10) {
      BLOCK_CALL(sys_hbridge_set_drive(d->device_id, d->channel, (float)drive_x10 / 1000.0f), b);
      if (unlikely(vm_block_failed(b))) {
        vm_block_set_eno(b, false);
        return;
      }
      d->last = VM_HBRIDGE_LAST_DRIVE;
      d->last_x10 = drive_x10;
    }
    vm_block_set_eno(b, true);
    return;
  }

  if (d->when_not_active != VM_HBRIDGE_IDLE_HOLD) hbridge_release(b, d, d->when_not_active == VM_HBRIDGE_IDLE_BRAKE);
  vm_block_set_eno(b, false);
}
