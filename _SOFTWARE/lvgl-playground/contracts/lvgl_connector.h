#pragma once

#include "stdint.h"
#include "sys_device.h"

//konektor opakowuje surowe lvgl: LVGL jest zainicjowany na starcie i pusty, INSTALL podłącza wyświetlacz (i dotyk),
//RESET rozbiera wszystko. Bufor rysowania konektor wylicza sam z DISPLAY GEOMETRY.
//Komendy pod własnym ROUTER_KEY (następny wolny w router_ids.h). Nigdy nie renumerować.


#define SYS_LVGL_OP_INSTALL  0x01u  // sys_lvgl_install_t
#define SYS_LVGL_OP_RESET    0x02u  // write only, no args
#define SYS_LVGL_OP_ROTATION 0x03u  // sys_lvgl_rotation_t
#define SYS_LVGL_OP_STATUS   0x04u  // read only: sys_lvgl_status_t
#define SYS_LVGL_OP_START    0x05u  // write only, no args: wznawia renderowanie i odczyty dotyku, unieważnia cały ekran
#define SYS_LVGL_OP_STOP     0x06u  // write only, no args: czeka na ostatni FLUSH_DONE, zatrzymuje renderowanie i dotyk

//INSTALL (zapis): buduje wszystko; przy zainstalowanym odrzucony (najpierw RESET). Odczyt: co jest zainstalowane
//(display i touch = SYS_DEVICE_ID_NONE, heap_bytes = 0, gdy nic). Po INSTALL konektor jest uruchomiony.
typedef struct __attribute__((packed)) {
  uint16_t display;     //id utworzonego urządzenia - wyświetlacz
  uint16_t touch;       //id utworzonego urządzenia - dotyk, albo SYS_DEVICE_ID_NONE: bez dotyku
  uint32_t heap_bytes;  //heap na alokację obiektów lvgl (widgety, które mają być wyświetlane); 0: domyślny z Kconfig
} sys_lvgl_install_t;

//obrót do renderu
typedef struct __attribute__((packed)) {
  uint8_t rotation;  /**< 0, 1, 2, 3: 0, 90, 180, 270 degrees */
} sys_lvgl_rotation_t;

//info co się dzieje z komponentem
typedef struct __attribute__((packed)) {
  uint8_t installed;       /**< 1 once INSTALL is done */
  uint8_t running;         /**< 1 while rendering and touch run: after INSTALL and START, 0 after STOP and RESET */
  uint16_t width;          /**< With the rotation */
  uint16_t height;
  uint32_t buffer_bytes;   /**< One draw buffer, as worked out */
  uint32_t heap_total;     /**< The objects' heap (the bootstrap one when not installed) */
  uint32_t heap_free;
  uint32_t flushes;        /**< FLUSH_DONE received */
  uint32_t flush_errors;   /**< Flushes that failed (the previous transfer's error, SYS_DISPLAY FLUSH) */
} sys_lvgl_status_t;

//Interfejs C, bez drutu: to samo, co wołają ops routera, plus blokada dla warstwy 4 i dla bootu. Obiektów LVGL (lv_*)
//dotyka się tylko pod blokadą, z dowolnego zadania poza zadaniem LVGL.

/** @brief Boot step: lv_init, the tick, the LVGL task, the small bootstrap heap; no display. Nothing is drawn yet. */
MUST_USE err_h sys_lvgl_init(void);

// blokada " na własnść - mutex- dla rezty warstw"
MUST_USE err_h sys_lvgl_lock(uint32_t timeout_ms);
void sys_lvgl_unlock(void);

//wrpappery na opsy - to samo co przez router 
MUST_USE err_h sys_lvgl_install(const sys_lvgl_install_t* install);
MUST_USE err_h sys_lvgl_get_install(sys_lvgl_install_t* out);
MUST_USE err_h sys_lvgl_reset(void);
MUST_USE err_h sys_lvgl_start(void);
MUST_USE err_h sys_lvgl_stop(void);
MUST_USE err_h sys_lvgl_set_rotation(uint8_t rotation);
MUST_USE err_h sys_lvgl_get_rotation(uint8_t* out);
MUST_USE err_h sys_lvgl_get_status(sys_lvgl_status_t* out);
