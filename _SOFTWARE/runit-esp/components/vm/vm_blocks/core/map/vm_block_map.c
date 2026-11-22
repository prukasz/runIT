#include "vm_block_map.h"
#include "vm_blocks.h"

bool vm_verify_map(vm_block_h b) {
  vm_block_map_data_t d;
  memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
  return isfinite(d.in_min) && isfinite(d.in_max) && isfinite(d.out_min) && isfinite(d.out_max) &&
         d.in_min != d.in_max && d.clamp <= 1 && d.curve < VM_MAP_CURVE_CNT;
}

/* The curve on t in [0, 1] (held there by the caller for every curve but LINEAR). */
static float vm_map_curve(uint8_t curve, float t) {
  switch (curve) {
    case VM_MAP_LOG:
      return log10f(1.0f + 9.0f * t);
    case VM_MAP_EXP:
      return (powf(10.0f, t) - 1.0f) / 9.0f;
    case VM_MAP_SQUARE:
      return t * t;
    case VM_MAP_SQRT:
      return sqrtf(t);
    case VM_MAP_SMOOTH:
      return t * t * (3.0f - 2.0f * t);
    default:
      return t;
  }
}

/* Pure: Y for X with the state's ends and curve (checked at load). */
static float vm_map_value(const vm_block_map_data_t* d, float x) {
  float t = (x - d->in_min) / (d->in_max - d->in_min);
  if (d->clamp || d->curve != VM_MAP_LINEAR) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
  }
  return d->out_min + vm_map_curve(d->curve, t) * (d->out_max - d->out_min);
}

void vm_blk_map(vm_block_h b) {
  IF_BLOCK_ENABLED(b) {
    vm_block_map_data_t d;
    memcpy(&d, vm_block_get_custom_data(b), sizeof(d));
    float x = 0;
    if (vm_block_check(b, VM_OBJ_SCALAR_GET(x, vm_block_get_inputs(b)[VM_IN_MAP_X]))) {
      const float y = vm_map_value(&d, x);
      BLOCK_CALL(VM_OBJ_SET_SCALAR(y, vm_block_get_outputs(b)[VM_OUT_MAP_Y]), b);
    }
    vm_block_set_eno(b, !vm_block_failed(b));
    return;
  }
  vm_block_set_eno(b, false);
}

VM_BLOCK_TYPE_DEFINE(VM_BLK_MAP, .run = vm_blk_map, .check = vm_verify_map, .min_in = 1, .max_in = 1, .min_q = 1,
                     .max_q = 1, .required_in = 0x1u, .state_len = sizeof(vm_block_map_data_t));
