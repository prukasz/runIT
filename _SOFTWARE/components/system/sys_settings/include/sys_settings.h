#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "sys_error.h"

/**
 * @file sys_settings.h
 * @brief Persistent settings: named binary records in one NVS namespace.
 *
 * Any module keeps its settings here under its own key: at most 15
 * characters, prefixed with the module (for example "pwr_psu"). A record is
 * read back only if its stored size matches the requested size, so a changed
 * struct layout reads as "not stored" instead of as garbage.
 */

/**
 * @brief Initialize NVS and open the settings namespace. Boot step, before
 * any module loads its settings.
 */
SE_MUST_USE err_h sys_settings_init(void);

/**
 * @brief Read record @p key into @p out.
 *
 * @param out_found Set to true only if the record exists with exactly @p size bytes.
 * @return err_h NULL when found or not stored; ERR_SETTINGS_SIZE_MISMATCH when
 *               stored with another size (out_found is false, out is untouched).
 */
SE_MUST_USE err_h sys_settings_load(const char* key, void* out, size_t size, bool* out_found);

/**
 * @brief Read a variable-length record @p key into @p out (up to @p capacity bytes).
 *
 * For records whose length varies with their content (the VM's retained values).
 *
 * @param out_len Receives the stored length when found.
 * @param out_found Set to true only if the record exists and fits.
 * @return err_h NULL when found or not stored; ERR_SETTINGS_SIZE_MISMATCH when
 *               the stored record is longer than @p capacity (out untouched).
 */
SE_MUST_USE err_h sys_settings_load_blob(const char* key, void* out, size_t capacity, size_t* out_len, bool* out_found);

/** @brief Write record @p key and commit it. */
SE_MUST_USE err_h sys_settings_store(const char* key, const void* data, size_t size);

/** @brief Remove record @p key and commit. Removing a missing record is not an error. */
SE_MUST_USE err_h sys_settings_erase(const char* key);
