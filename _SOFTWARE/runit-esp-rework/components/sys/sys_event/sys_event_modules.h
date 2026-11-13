#pragma once

/**
 * @file sys_event_modules.h
 * @brief Who owns which module of a tag (EVT_TAG(module, code)). Three ranges, so no two publishers share one and a
 * tag alone says what the event is:
 *   0x0001..0x00FE a module: its number (MODULE_ID_<NAME>, module_id.h), the same as its router key and the module of
 *                  its errors; a device contract's events carry the contract's ID, which is its module's number
 *   0x8000..0xFFFE a device type's own events: SYS_EVENT_MODULE_TYPE(its type ID), in the driver's .c, from the
 *                  NAME_TYPE its SYS_DEVICE_TYPE_DECLARE gives; the type ID is unique, so the module is
 *   0xFFFF         the filters' (SYS_EVENT_ANY_TAG, SYS_EVENT_ANY_NOTIFY): never a module
 * 0x0000 and 0x00FF are no module: a device's own ops are 0xFFxx, its own events are its type's. The ranges between
 * 0x0100 and 0x7FFF are free.
 */

/** @brief The module of the own events of device type `type` (its SYS_DEVICE_TYPE_DECLARE ID, 0x0040..0x7FFE). */
#define SYS_EVENT_MODULE_TYPE(type) ((uint16_t)(0x8000u | (type)))
