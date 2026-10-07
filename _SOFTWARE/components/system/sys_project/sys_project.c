#include "sys_project.h"
#include <esp_log.h>
#include <esp_rom_crc.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <string.h>
#include "sys_interface.h"
#include "sys_settings.h"
#include "utils.h"

/*
 * Storage (partition CONFIG_SYS_PROJECT_PARTITION_LABEL, namespace "code"), two independent
 * slots, each with its own list, generations, CRC and header:
 *   program slot (no key prefix)   "<g>000", "<g>001", ... and "active"
 *   config slot (prefix "c")       "c<g>000", ... and "cactive"
 * Chunks hold the frame list, CONFIG_SYS_PROJECT_CHUNK_SIZE bytes each (the last shorter);
 * generation g = 'a' / 'b'; "active" is the project_header_t of the committed generation.
 * A store fills the generation that isn't active; commit checks it and rewrites
 * "active" in one NVS write, so an interrupted store keeps the old code. The old
 * generation's chunks are erased by the next begin. The program slot keeps the
 * key names of the former single list.
 *
 * Every call comes from the interface RX task (decoders), except the boot replay,
 * which runs with RX suspended; no lock is needed.
 */

static const char* TAG = "sys_project";

#define PROJECT_NAMESPACE "code"
#define PROJECT_KEY_ACTIVE "active"
#define PROJECT_SETTINGS_KEY "prj_opts"  // sys_settings, default NVS partition
#define PROJECT_MAGIC 0x4A525052u         // "RPRJ" little-endian
#define PROJECT_CHUNK CONFIG_SYS_PROJECT_CHUNK_SIZE
/* Stored frames carry no seq byte, so they're one byte shorter than the connector's frame limit. */
#define PROJECT_FRAME_MAX (CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX - 1)

typedef struct {
  uint32_t magic;
  uint32_t length;
  uint32_t crc32;
  uint32_t schema_id;
  uint16_t frame_count;
  char generation;  // 'a' or 'b'
  uint8_t reserved;
} project_header_t;

typedef struct {
  uint8_t autostart;
} project_options_t;

typedef struct {
  const char* prefix;  // key prefix of the slot's chunks and header
  uint32_t max_bytes;
  project_header_t active;
  bool has_active;
} slot_t;

static slot_t s_slots[SYS_PROJECT_SLOT_COUNT] = {
    [SYS_PROJECT_SLOT_CONFIG] = {.prefix = "c", .max_bytes = CONFIG_SYS_PROJECT_CONFIG_MAX_BYTES},
    [SYS_PROJECT_SLOT_PROGRAM] = {.prefix = "", .max_bytes = CONFIG_SYS_PROJECT_MAX_BYTES},
};

static nvs_handle_t s_nvs;
static bool s_ready;

/* Store in progress (one at a time, whatever the slot). */
static bool s_storing;
static sys_project_slot_e s_store_slot;
static char s_store_gen;
static uint32_t s_store_length;
static uint32_t s_received;
static uint16_t s_chunk_index;
static size_t s_chunk_fill;
static uint8_t s_chunk[PROJECT_CHUNK];

/* Read cache: one chunk of a slot's generation. */
static uint8_t s_cache[PROJECT_CHUNK];
static int s_cache_slot = -1;
static char s_cache_gen;
static int32_t s_cache_index = -1;

static uint8_t s_frame[PROJECT_FRAME_MAX];
static sys_project_report_t s_report = {.first_failed = UINT16_MAX};

static void restart_cb(TimerHandle_t timer) {
  (void)timer;
  esp_restart();
}
R_TIMER_DEFINE(sys_project_restart_timer, pdMS_TO_TICKS(CONFIG_SYS_PROJECT_LOAD_DELAY_MS), false, restart_cb);

static void chunk_key(sys_project_slot_e slot, char generation, uint16_t index, char* key, size_t size) {
  snprintf(key, size, "%s%c%03u", s_slots[slot].prefix, generation, (unsigned)index);
}

static void active_key(sys_project_slot_e slot, char* key, size_t size) {
  snprintf(key, size, "%s" PROJECT_KEY_ACTIVE, s_slots[slot].prefix);
}

static bool slot_valid(uint8_t slot) {
  return slot < SYS_PROJECT_SLOT_COUNT;
}

static char other_gen(char generation) {
  return generation == 'a' ? 'b' : 'a';
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

static const char* slot_name(sys_project_slot_e slot) {
  return slot == SYS_PROJECT_SLOT_CONFIG ? "config" : "program";
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_INIT
err_h sys_project_init(void) {
  if (s_ready) return NULL;
  esp_err_t rc = nvs_flash_init_partition(CONFIG_SYS_PROJECT_PARTITION_LABEL);
  if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // An unreadable store only holds a copy of the app's project: start empty.
    ESP_LOGW(TAG, "project partition unreadable (%s), erasing it", esp_err_to_name(rc));
    SE_TRY_ESP(nvs_flash_erase_partition(CONFIG_SYS_PROJECT_PARTITION_LABEL));
    rc = nvs_flash_init_partition(CONFIG_SYS_PROJECT_PARTITION_LABEL);
  }
  SE_TRY_ESP(rc);
  SE_TRY_ESP(nvs_open_from_partition(CONFIG_SYS_PROJECT_PARTITION_LABEL, PROJECT_NAMESPACE, NVS_READWRITE, &s_nvs));

  for (int i = 0; i < SYS_PROJECT_SLOT_COUNT; i++) {
    slot_t* slot = &s_slots[i];
    char key[16];
    active_key((sys_project_slot_e)i, key, sizeof(key));
    size_t size = sizeof(slot->active);
    rc = nvs_get_blob(s_nvs, key, &slot->active, &size);
    slot->has_active = rc == ESP_OK && size == sizeof(slot->active) && slot->active.magic == PROJECT_MAGIC && (slot->active.generation == 'a' || slot->active.generation == 'b');
    if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) SE_TRY_ESP(rc);
    ESP_LOGI(TAG, "stored %s: %s", slot_name((sys_project_slot_e)i), slot->has_active ? "present" : "none");
    if (slot->has_active) ESP_LOGI(TAG, "%lu bytes, %u frames, CRC 0x%08lX", (unsigned long)slot->active.length, slot->active.frame_count, (unsigned long)slot->active.crc32);
  }
  s_ready = true;
  return NULL;
}

// ---------------------------------------------------------------------------
// Chunk reading (shared by commit checks, read and replay)
// ---------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_READ
/* Chunk `index` of a slot's generation holding `length` bytes into the cache; `out_len` = its size. */
static SE_MUST_USE err_h load_chunk(sys_project_slot_e slot, char generation, uint32_t length, uint16_t index, size_t* out_len) {
  const uint32_t chunk_start = (uint32_t)index * PROJECT_CHUNK;
  *out_len = (length - chunk_start) < PROJECT_CHUNK ? (length - chunk_start) : PROJECT_CHUNK;
  if (s_cache_slot == (int)slot && s_cache_gen == generation && s_cache_index == index) return NULL;
  char key[16];
  chunk_key(slot, generation, index, key, sizeof(key));
  size_t size = *out_len;
  s_cache_index = -1;
  SE_TRY_ESP(nvs_get_blob(s_nvs, key, s_cache, &size));
  s_cache_slot = (int)slot;
  s_cache_gen = generation;
  s_cache_index = index;
  return NULL;
}

/* Bytes [offset, offset + len) of a generation holding `length` bytes; the caller checked the range. */
static SE_MUST_USE err_h read_gen(sys_project_slot_e slot, char generation, uint32_t length, uint32_t offset, uint8_t* out, size_t len) {
  while (len > 0) {
    const uint16_t index = (uint16_t)(offset / PROJECT_CHUNK);
    const uint32_t chunk_start = (uint32_t)index * PROJECT_CHUNK;
    size_t chunk_len = 0;
    SE_TRY(load_chunk(slot, generation, length, index, &chunk_len));
    const size_t at = offset - chunk_start;
    const size_t take = (chunk_len - at) < len ? (chunk_len - at) : len;
    memcpy(out, &s_cache[at], take);
    out += take;
    offset += take;
    len -= take;
  }
  return NULL;
}

static SE_MUST_USE err_h crc_gen(sys_project_slot_e slot, char generation, uint32_t length, uint32_t* out_crc) {
  uint32_t crc = 0;
  for (uint16_t index = 0; (uint32_t)index * PROJECT_CHUNK < length; index++) {
    size_t chunk_len = 0;
    SE_TRY(load_chunk(slot, generation, length, index, &chunk_len));
    crc = esp_rom_crc32_le(crc, s_cache, (uint32_t)chunk_len);
  }
  *out_crc = crc;
  return NULL;
}

/* Walk the frames: each [u16 len][frame] must fit, be 2..PROJECT_FRAME_MAX bytes and pass `filter`. */
static SE_MUST_USE err_h check_frames(sys_project_slot_e slot, char generation, uint32_t length, sys_project_frame_filter_f filter, uint16_t* out_count) {
  uint32_t offset = 0;
  uint16_t index = 0;
  while (offset < length) {
    uint16_t frame_len = 0;
    if (offset + sizeof(frame_len) > length) SE_FAIL(ERR_PROJECT_BAD_FRAME, .index = index, .offset = offset, .len = 0);
    SE_TRY(read_gen(slot, generation, length, offset, (uint8_t*)&frame_len, sizeof(frame_len)));
    if (frame_len < 2 || frame_len > PROJECT_FRAME_MAX || offset + sizeof(frame_len) + frame_len > length || index == UINT16_MAX) {
      SE_FAIL(ERR_PROJECT_BAD_FRAME, .index = index, .offset = offset, .len = frame_len);
    }
    SE_TRY(read_gen(slot, generation, length, offset + sizeof(frame_len), s_frame, frame_len));
    if (filter && !filter(slot, s_frame, frame_len)) SE_FAIL(ERR_PROJECT_FRAME_REFUSED, .index = index, .class_header = s_frame[0], .packet_header = s_frame[1]);
    offset += sizeof(frame_len) + frame_len;
    index++;
  }
  *out_count = index;
  return NULL;
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_STORE
static SE_MUST_USE err_h erase_gen(sys_project_slot_e slot, char generation) {
  for (uint16_t index = 0;; index++) {
    char key[16];
    chunk_key(slot, generation, index, key, sizeof(key));
    esp_err_t rc = nvs_erase_key(s_nvs, key);
    if (rc == ESP_ERR_NVS_NOT_FOUND) break;
    SE_TRY_ESP(rc);
  }
  if (s_cache_slot == (int)slot && s_cache_gen == generation) s_cache_index = -1;
  SE_TRY_ESP(nvs_commit(s_nvs));
  return NULL;
}

static SE_MUST_USE err_h flush_chunk(void) {
  if (s_chunk_fill == 0) return NULL;
  char key[16];
  chunk_key(s_store_slot, s_store_gen, s_chunk_index, key, sizeof(key));
  SE_TRY_ESP(nvs_set_blob(s_nvs, key, s_chunk, s_chunk_fill));
  s_chunk_index++;
  s_chunk_fill = 0;
  return NULL;
}

err_h sys_project_begin(sys_project_slot_e slot, uint32_t length) {
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  if (!slot_valid(slot)) SE_FAIL(ERR_PROJECT_BAD_SLOT, .slot = (uint8_t)slot);
  const slot_t* info = &s_slots[slot];
  if (length > info->max_bytes) SE_FAIL(ERR_PROJECT_TOO_BIG, .length = length, .capacity = info->max_bytes);
  s_storing = false;
  s_store_slot = slot;
  s_store_gen = info->has_active ? other_gen(info->active.generation) : 'a';
  SE_TRY(erase_gen(slot, s_store_gen));
  s_store_length = length;
  s_received = 0;
  s_chunk_index = 0;
  s_chunk_fill = 0;
  s_storing = true;
  ESP_LOGI(TAG, "storing %s: %lu bytes into generation %c", slot_name(slot), (unsigned long)length, s_store_gen);
  return NULL;
}

err_h sys_project_write(uint32_t offset, const uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  if (!s_storing) SE_FAIL(ERR_PROJECT_NOT_STORING, 0);
  if (offset != s_received) SE_FAIL(ERR_PROJECT_OFFSET, .offset = offset, .expected = s_received);
  if (s_received + len > s_store_length) SE_FAIL(ERR_PROJECT_TOO_BIG, .length = (uint32_t)(s_received + len), .capacity = s_store_length);
  while (len > 0) {
    const size_t take = (PROJECT_CHUNK - s_chunk_fill) < len ? (PROJECT_CHUNK - s_chunk_fill) : len;
    memcpy(&s_chunk[s_chunk_fill], data, take);
    s_chunk_fill += take;
    s_received += (uint32_t)take;
    data += take;
    len -= take;
    if (s_chunk_fill == PROJECT_CHUNK) SE_TRY(flush_chunk());
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_COMMIT
err_h sys_project_commit(uint32_t crc32, uint32_t schema_id, sys_project_frame_filter_f filter) {
  if (!s_storing) SE_FAIL(ERR_PROJECT_NOT_STORING, 0);
  s_storing = false;  // a failed commit needs a new begin
  if (s_received != s_store_length) SE_FAIL(ERR_PROJECT_INCOMPLETE, .received = s_received, .length = s_store_length);
  if (schema_id != SE_schema_id()) SE_FAIL(ERR_PROJECT_SCHEMA, .stored = schema_id, .firmware = SE_schema_id());
  SE_TRY(flush_chunk());
  SE_TRY_ESP(nvs_commit(s_nvs));

  // Check what was written, read back from flash.
  s_cache_index = -1;
  uint32_t crc = 0;
  SE_TRY(crc_gen(s_store_slot, s_store_gen, s_store_length, &crc));
  if (crc != crc32) SE_FAIL(ERR_PROJECT_CRC, .crc = crc, .expected = crc32);
  uint16_t count = 0;
  SE_TRY(check_frames(s_store_slot, s_store_gen, s_store_length, filter, &count));

  const project_header_t header = {
      .magic = PROJECT_MAGIC,
      .length = s_store_length,
      .crc32 = crc,
      .schema_id = schema_id,
      .frame_count = count,
      .generation = s_store_gen,
  };
  char key[16];
  active_key(s_store_slot, key, sizeof(key));
  SE_TRY_ESP(nvs_set_blob(s_nvs, key, &header, sizeof(header)));
  SE_TRY_ESP(nvs_commit(s_nvs));
  slot_t* slot = &s_slots[s_store_slot];
  slot->active = header;
  slot->has_active = true;
  ESP_LOGI(TAG, "stored %s committed: %lu bytes, %u frames, CRC 0x%08lX", slot_name(s_store_slot), (unsigned long)header.length, header.frame_count, (unsigned long)header.crc32);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_STORE
err_h sys_project_abort(void) {
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  if (!s_storing) return NULL;
  s_storing = false;
  return erase_gen(s_store_slot, s_store_gen);
}

err_h sys_project_erase(sys_project_slot_e slot) {
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  if (!slot_valid(slot)) SE_FAIL(ERR_PROJECT_BAD_SLOT, .slot = (uint8_t)slot);
  if (s_storing && s_store_slot == slot) s_storing = false;
  char key[16];
  active_key(slot, key, sizeof(key));
  esp_err_t rc = nvs_erase_key(s_nvs, key);
  if (rc != ESP_ERR_NVS_NOT_FOUND) SE_TRY_ESP(rc);
  SE_TRY_ESP(nvs_commit(s_nvs));
  s_slots[slot].has_active = false;
  SE_TRY(erase_gen(slot, 'a'));
  SE_TRY(erase_gen(slot, 'b'));
  ESP_LOGI(TAG, "stored %s erased", slot_name(slot));
  return NULL;
}

err_h sys_project_set_options(bool autostart) {
  const project_options_t options = {.autostart = autostart ? 1 : 0};
  return sys_settings_store(PROJECT_SETTINGS_KEY, &options, sizeof(options));
}

static bool autostart_set(void) {
  project_options_t options = {0};
  bool found = false;
  // A missing or old-layout record means "off"; the size mismatch is reported by sys_settings.
  SE_REPORT(sys_settings_load(PROJECT_SETTINGS_KEY, &options, sizeof(options), &found));
  return found && options.autostart != 0;
}

err_h sys_project_get_info(sys_project_slot_e slot, sys_project_info_t* out) {
  SE_CHECK_NOT_NULL(out);
  if (!slot_valid(slot)) SE_FAIL(ERR_PROJECT_BAD_SLOT, .slot = (uint8_t)slot);
  const slot_t* info = &s_slots[slot];
  *out = (sys_project_info_t){
      .stored = info->has_active,
      .length = info->has_active ? info->active.length : 0,
      .frame_count = info->has_active ? info->active.frame_count : 0,
      .crc32 = info->has_active ? info->active.crc32 : 0,
      .schema_id = info->has_active ? info->active.schema_id : 0,
      .capacity = info->max_bytes,
      .autostart = autostart_set(),
      .report = s_report,
  };
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_READ
err_h sys_project_read(sys_project_slot_e slot, uint32_t offset, uint8_t* out, size_t len) {
  SE_CHECK_NOT_NULL(out);
  if (!slot_valid(slot)) SE_FAIL(ERR_PROJECT_BAD_SLOT, .slot = (uint8_t)slot);
  const slot_t* info = &s_slots[slot];
  if (!info->has_active) SE_FAIL(ERR_PROJECT_NOT_STORED, 0);
  if (offset > info->active.length || len > info->active.length - offset) SE_FAIL(ERR_PROJECT_READ_RANGE, .offset = offset, .len = (uint32_t)len, .length = info->active.length);
  return read_gen(slot, info->active.generation, info->active.length, offset, out, len);
}

// ---------------------------------------------------------------------------
// Boot replay and load
// ---------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_REPLAY
/* Replay one slot's list. The report keeps the worst state over the slots (corrupt > skipped > replayed). */
static err_h replay_slot(sys_project_slot_e slot, bool (*is_vm_frame)(const uint8_t* frame, size_t len)) {
  const slot_t* info = &s_slots[slot];
  if (!info->has_active) return NULL;
  const project_header_t* active = &info->active;
  s_report.crc32[slot] = active->crc32;
  if (active->schema_id != SE_schema_id()) {
    if (s_report.state < SYS_PROJECT_REPLAY_SKIPPED) s_report.state = SYS_PROJECT_REPLAY_SKIPPED;
    SE_FAIL(ERR_PROJECT_SCHEMA, .stored = active->schema_id, .firmware = SE_schema_id());
  }
  uint32_t crc = 0;
  uint16_t count = 0;
  err_h err = crc_gen(slot, active->generation, active->length, &crc);
  if (!err && crc != active->crc32) err = SE_ERR_NEW(ERR_PROJECT_CRC, .crc = crc, .expected = active->crc32);
  if (!err) err = check_frames(slot, active->generation, active->length, NULL, &count);
  if (err) {
    s_report.state = SYS_PROJECT_REPLAY_CORRUPT;
    SE_TRY(err);
  }

  ESP_LOGI(TAG, "replaying stored %s: %u frames", slot_name(slot), count);
  uint32_t offset = 0;
  for (uint16_t index = 0; index < count; index++) {
    uint16_t frame_len = 0;
    err = read_gen(slot, active->generation, active->length, offset, (uint8_t*)&frame_len, sizeof(frame_len));
    if (!err) err = read_gen(slot, active->generation, active->length, offset + sizeof(frame_len), s_frame, frame_len);
    if (err) break;  // flash read failed: the rest can't be trusted
    offset += sizeof(frame_len) + frame_len;

    err_h frame_err = sys_interface_decode(s_frame, frame_len);
    if (!frame_err) {
      s_report.applied++;
      continue;
    }
    if (s_report.failed == 0) {
      s_report.first_failed = index;
      s_report.first_slot = slot;
      s_report.first_tag = (uint16_t)frame_err->tag;
      s_report.first_owner = (uint16_t)frame_err->owner;
    }
    s_report.failed++;
    if (is_vm_frame && is_vm_frame(s_frame, frame_len)) s_report.vm_failed = true;
    SE_REPORT(frame_err);
  }
  if (s_report.state < SYS_PROJECT_REPLAY_DONE) s_report.state = SYS_PROJECT_REPLAY_DONE;
  ESP_LOGI(TAG, "stored %s replayed: %u applied, %u refused so far", slot_name(slot), s_report.applied, s_report.failed);
  SE_TRY(err);
  return NULL;
}

err_h sys_project_replay(bool (*is_vm_frame)(const uint8_t* frame, size_t len)) {
  s_report = (sys_project_report_t){.state = SYS_PROJECT_REPLAY_NONE, .first_failed = UINT16_MAX};
  sys_interface_suspend_rx();
  err_h first = NULL;
  for (int slot = 0; slot < SYS_PROJECT_SLOT_COUNT; slot++) {
    err_h err = replay_slot((sys_project_slot_e)slot, is_vm_frame);
    if (!err) continue;
    if (!first) first = err;
    else SE_REPORT(err);
  }
  sys_interface_resume_rx();
  return first;
}

void sys_project_report(sys_project_report_t* out, bool* out_autostart) {
  if (out) *out = s_report;
  if (out_autostart) *out_autostart = autostart_set();
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_BASE
err_h sys_project_load(void) {
  if (R_TIMER_START(sys_project_restart_timer) != pdPASS) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  ESP_LOGI(TAG, "restarting in %d ms to load the stored code", CONFIG_SYS_PROJECT_LOAD_DELAY_MS);
  return NULL;
}
