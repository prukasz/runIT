#pragma once

/* The fonts LVGL has (lv_conf.h). A font left out here does not exist on the board; the screen layer's font enum
 * (sys_screen_font_e: 12, 14, 16, 24) names only these, and once it references them each costs flash (tens of KB; the
 * linker drops a font nothing references). */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14
