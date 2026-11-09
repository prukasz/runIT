/* A dummy screen in place of sys_screen, which does not exist yet: built in C under sys_lvgl_lock, as sys_screen's
 * commands will. A title, a button that counts, a slider with a bar, a switch and the adapter's STATUS (heap, flushes)
 * every half second. The adapter's RESET deletes it (the reset hook). */
#pragma once

/* Builds it on the active screen; the adapter must be installed. Returns 0, or -1 when it is not. */
int sim_demo_build(void);

/* Adds up to `count` labels to the active screen, each after sys_lvgl_pool_check, as sys_screen will; prints the pool
   after every 50 and stops at the first refusal. Returns how many were made, or -1 when the screen is not installed. */
int sim_demo_fill(int count);

/* Registers the reset hook (before INSTALL). */
void sim_demo_init(void);
