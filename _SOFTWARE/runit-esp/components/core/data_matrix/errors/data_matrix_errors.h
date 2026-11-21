#pragma once

#include "errors.h"
#include "data_matrix_id.h"

// The switch's own: a provider's failure is its ERR_PROV_*.
#define ERR_DATA_MATRIX_MAP(X, M)                                                                                       \
  X(M, ERR_DATA_MATRIX_FULL,    1, ERR_SEV_MEDIUM, {}) /* every place, or a port's every target, taken */               \
  X(M, ERR_DATA_MATRIX_BAD_ARG, 2, ERR_SEV_HIGH,   {}) /* unknown port, name taken, a port to itself, no input */

ERR_TAGS_DECLARE(MODULE_ID_DATA_MATRIX, ERR_DATA_MATRIX_MAP)
