#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |    MAP    | ->ENO
 *  ->X      |           | ->Y
 *           -------------
 *
 *  VM_BLK_MAP -- X from [in_min, in_max] onto [out_min, out_max] along a curve:
 *    t = (X - in_min) / (in_max - in_min),   Y = out_min + curve(t) * (out_max - out_min)
 *  The curve takes 0 to 0 and 1 to 1 (vm_map_curve_e); the pairs are each other's inverse, so a value mapped
 *  one way and back is the same. The four ends are constants of the state (floats). A range may run downwards
 *  (in_min > in_max or out_min > out_max): the map then inverts. With `clamp` t is held to [0, 1] first, so Y never
 *  leaves [out_min, out_max]; without it a linear map goes on past the ends (any other curve is always held: it is
 *  not defined, or not useful, outside [0, 1]). Not enabled: Y keeps its value, ENO off.
 */

typedef enum vm_in_map_e {
  VM_IN_MAP_X = 0,
} vm_in_map_e;

typedef enum vm_out_map_e {
  VM_OUT_MAP_Y = 0,
} vm_out_map_e;

/* The shape of the map: t in [0, 1] -> [0, 1]. A program holds the number: never renumber. */
typedef enum vm_map_curve_e {
  VM_MAP_LINEAR = 0,  // t
  VM_MAP_LOG = 1,     // log10(1 + 9t): rises fast, then flattens (a volume knob, a sensor that saturates)
  VM_MAP_EXP = 2,     // (10^t - 1) / 9: starts slow, then rises fast (the inverse of LOG)
  VM_MAP_SQUARE = 3,  // t^2: gentle start, fine control at low values (a throttle)
  VM_MAP_SQRT = 4,    // sqrt(t): steep start (the inverse of SQUARE)
  VM_MAP_SMOOTH = 5,  // 3t^2 - 2t^3: eases in and out (an S-curve)
  VM_MAP_CURVE_CNT
} vm_map_curve_e;

typedef struct __attribute__((aligned(4))) {
  float in_min;   // The value of X that maps to out_min
  float in_max;   // The value of X that maps to out_max (not equal to in_min)
  float out_min;  // Y at in_min
  float out_max;  // Y at in_max
  uint8_t clamp;  // 1: X is held to [in_min, in_max], so Y stays in [out_min, out_max]
  uint8_t curve;  // vm_map_curve_e
  uint8_t _pad[2];
} vm_block_map_data_t;

_Static_assert(sizeof(vm_block_map_data_t) == 20, "vm_block_map_data_t must be 20 bytes");
_Static_assert(offsetof(vm_block_map_data_t, clamp) == 16, "clamp must be at offset 16");
_Static_assert(offsetof(vm_block_map_data_t, curve) == 17, "curve must be at offset 17");

/* Load-time check of the ends (finite, in_min != in_max); the body, called every pass. */
bool vm_verify_map(vm_block_h b);
void vm_blk_map(vm_block_h b);
