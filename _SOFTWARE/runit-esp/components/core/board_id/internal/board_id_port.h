#pragma once

/* What the board's identity takes from this chip (esp/board_id_esp.c), once at board_id_init. Not on the public include
   path. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A random number: the boot id. */
uint32_t board_id_port_random(void);

/** @brief The first 4 bytes of the firmware's ELF SHA-256. */
uint32_t board_id_port_build(void);

#ifdef __cplusplus
}
#endif
