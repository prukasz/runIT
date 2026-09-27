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
 * Storage (partition CONFIG_SYS_PROJECT_PARTITION_LABEL, namespace "code"):
 *   "<g>000", "<g>001", ...  chunks of the frame list, CONFIG_SYS_PROJECT_CHUNK_SIZE
 *                            bytes each (the last shorter); generation g = 'a' / 'b'
 *   "active"                 project_header_t of the committed generation
 * A store fills the generation that isn't active; commit checks it and rewrites
 * "active" in one NVS write, so an interrupted store keeps the old code. The old
 * generation's chunks are erased by the next begin.
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

static nvs_handle_t s_nvs;
static bool s_ready;
static project_header_t s_active;
static bool s_has_active;

/* Store in progress. */
static bool s_storing;
static char s_store_gen;
static uint32_t s_store_length;
static uint32_t s_received;
static uint16_t s_chunk_index;
static size_t s_chunk_fill;
static uint8_t s_chunk[PROJECT_CHUNK];

/* Read cache: one chunk of a generation. */
static uint8_t s_cache[PROJECT_CHUNK];
static char s_cache_gen;
static int32_t s_cache_index = -1;

static uint8_t s_frame[PROJECT_FRAME_MAX];
static sys_project_report_t s_report = {.first_failed = UINT16_MAX};

static void restart_cb(TimerHandle_t timer) {
  (void)timer;
  esp_restart();
}
R_TIMER_DEFINE(sys_project_restart_timer, pdMS_TO_TICKS(CONFIG_SYS_PROJECT_LOAD_DELAY_MS), false, restart_cb);

static void chunk_key(char generation, uint16_t index, char* key, size_t size) {
  snprintf(key, size, "%c%03u", generation, (unsigned)index);
}

static char other_gen(char generation) {
  return generation == 'a' ? 'b' : 'a';
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

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

  size_t size = sizeof(s_active);
  rc = nvs_get_blob(s_nvs, PROJECT_KEY_ACTIVE, &s_active, &size);
  s_has_active = rc == ESP_OK && size == sizeof(s_active) && s_active.magic == PROJECT_MAGIC && (s_active.generation == 'a' || s_active.generation == 'b');
  if (rc != ESP_OK && rc != ESP_ERR_NVS_NOT_FOUND) SE_TRY_ESP(rc);
  s_ready = true;
  ESP_LOGI(TAG, "stored code: %s", s_has_active ? "present" : "none");
  if (s_has_active) ESP_LOGI(TAG, "%lu bytes, %u frames, CRC 0x%08lX", (unsigned long)s_active.length, s_active.frame_count, (unsigned long)s_active.crc32);
  return NULL;
}

// ---------------------------------------------------------------------------
// Chunk reading (shared by commit checks, read and replay)
// ---------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_READ
/* Chunk `index` of a generation holding `length` bytes into the cache; `out_len` = its size. */
static SE_MUST_USE err_h load_chunk(char generation, uint32_t length, uint16_t index, size_t* out_len) {
  const uint32_t chunk_start = (uint32_t)index * PROJECT_CHUNK;
  *out_len = (length - chunk_start) < PROJECT_CHUNK ? (length - chunk_start) : PROJECT_CHUNK;
  if (s_cache_gen == generation && s_cache_index == index) return NULL;
  char key[8];
  chunk_key(generation, index, key, sizeof(key));
  size_t size = *out_len;
  s_cache_index = -1;
  SE_TRY_ESP(nvs_get_blob(s_nvs, key, s_cache, &size));
  s_cache_gen = generation;
  s_cache_index = index;
  return NULL;
}

/* Bytes [offset, offset + len) of a generation holding `length` bytes; the caller checked the range. */
static SE_MUST_USE err_h read_gen(char generation, uint32_t length, uint32_t offset, uint8_t* out, size_t len) {
  while (len > 0) {
    const uint16_t index = (uint16_t)(offset / PROJECT_CHUNK);
    const uint32_t chunk_start = (uint32_t)index * PROJECT_CHUNK;
    size_t chunk_len = 0;
    SE_TRY(load_chunk(generation, length, index, &chunk_len));
    const size_t at = offset - chunk_start;
    const size_t take = (chunk_len - at) < len ? (chunk_len - at) : len;
    memcpy(out, &s_cache[at], take);
    out += take;
    offset += take;
    len -= take;
  }
  return NULL;
}

static SE_MUST_USE err_h crc_gen(char generation, uint32_t length, uint32_t* out_crc) {
  uint32_t crc = 0;
  for (uint16_t index = 0; (uint32_t)index * PROJECT_CHUNK < length; index++) {
    size_t chunk_len = 0;
    SE_TRY(load_chunk(generation, length, index, &chunk_len));
    crc = esp_rom_crc32_le(crc, s_cache, (uint32_t)chunk_len);
  }
  *out_crc = crc;
  return NULL;
}

/* Walk the frames: each [u16 len][frame] must fit, be 2..PROJECT_FRAME_MAX bytes and pass `filter`. */
static SE_MUST_USE err_h check_frames(char generation, uint32_t length, sys_project_frame_filter_f filter, uint16_t* out_count) {
  uint32_t offset = 0;
  uint16_t index = 0;
  while (offset < length) {
    uint16_t frame_len = 0;
    if (offset + sizeof(frame_len) > length) SE_FAIL(ERR_PROJECT_BAD_FRAME, .index = index, .offset = offset, .len = 0);
    SE_TRY(read_gen(generation, length, offset, (uint8_t*)&frame_len, sizeof(frame_len)));
    if (frame_len < 2 || frame_len > PROJECT_FRAME_MAX || offset + sizeof(frame_len) + frame_len > length || index == UINT16_MAX) {
      SE_FAIL(ERR_PROJECT_BAD_FRAME, .index = index, .offset = offset, .len = frame_len);
    }
    SE_TRY(read_gen(generation, length, offset + sizeof(frame_len), s_frame, frame_len));
    if (filter && !filter(s_frame, frame_len)) SE_FAIL(ERR_PROJECT_FRAME_REFUSED, .index = index, .class_header = s_frame[0], .packet_header = s_frame[1]);
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
static SE_MUST_USE err_h erase_gen(char generation) {
  for (uint16_t index = 0;; index++) {
    char key[8];
    chunk_key(generation, index, key, sizeof(key));
    esp_err_t rc = nvs_erase_key(s_nvs, key);
    if (rc == ESP_ERR_NVS_NOT_FOUND) break;
    SE_TRY_ESP(rc);
  }
  if (s_cache_gen == generation) s_cache_index = -1;
  SE_TRY_ESP(nvs_commit(s_nvs));
  return NULL;
}

static SE_MUST_USE err_h flush_chunk(void) {
  if (s_chunk_fill == 0) return NULL;
  char key[8];
  chunk_key(s_store_gen, s_chunk_index, key, sizeof(key));
  SE_TRY_ESP(nvs_set_blob(s_nvs, key, s_chunk, s_chunk_fill));
  s_chunk_index++;
  s_chunk_fill = 0;
  return NULL;
}

err_h sys_project_begin(uint32_t length) {
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  if (length > CONFIG_SYS_PROJECT_MAX_BYTES) SE_FAIL(ERR_PROJECT_TOO_BIG, .length = length, .capacity = CONFIG_SYS_PROJECT_MAX_BYTES);
  s_storing = false;
  s_store_gen = s_has_active ? other_gen(s_active.generation) : 'a';
  SE_TRY(erase_gen(s_store_gen));
  s_store_length = length;
  s_received = 0;
  s_chunk_index = 0;
  s_chunk_fill = 0;
  s_storing = true;
  ESP_LOGI(TAG, "storing code: %lu bytes into generation %c", (unsigned long)length, s_store_gen);
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
  SE_TRY(crc_gen(s_store_gen, s_store_length, &crc));
  if (crc != crc32) SE_FAIL(ERR_PROJECT_CRC, .crc = crc, .expected = crc32);
  uint16_t count = 0;
  SE_TRY(check_frames(s_store_gen, s_store_length, filter, &count));

  const project_header_t header = {
      .magic = PROJECT_MAGIC,
      .length = s_store_length,
      .crc32 = crc,
      .schema_id = schema_id,
      .frame_count = count,
      .generation = s_store_gen,
  };
  SE_TRY_ESP(nvs_set_blob(s_nvs, PROJECT_KEY_ACTIVE, &header, sizeof(header)));
  SE_TRY_ESP(nvs_commit(s_nvs));
  s_active = header;
  s_has_active = true;
  ESP_LOGI(TAG, "stored code committed: %lu bytes, %u frames, CRC 0x%08lX", (unsigned long)header.length, header.frame_count, (unsigned long)header.crc32);
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_STORE
err_h sys_project_abort(void) {
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  if (!s_storing) return NULL;
  s_storing = false;
  return erase_gen(s_store_gen);
}

err_h sys_project_erase(void) {
  if (!s_ready) SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  s_storing = false;
  esp_err_t rc = nvs_erase_key(s_nvs, PROJECT_KEY_ACTIVE);
  if (rc != ESP_ERR_NVS_NOT_FOUND) SE_TRY_ESP(rc);
  SE_TRY_ESP(nvs_commit(s_nvs));
  s_has_active = false;
  SE_TRY(erase_gen('a'));
  SE_TRY(erase_gen('b'));
  ESP_LOGI(TAG, "stored code erased");
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

err_h sys_project_get_info(sys_project_info_t* out) {
  SE_CHECK_NOT_NULL(out);
  *out = (sys_project_info_t){
      .stored = s_has_active,
      .length = s_has_active ? s_active.length : 0,
      .frame_count = s_has_active ? s_active.frame_count : 0,
      .crc32 = s_has_active ? s_active.crc32 : 0,
      .schema_id = s_has_active ? s_active.schema_id : 0,
      .capacity = CONFIG_SYS_PROJECT_MAX_BYTES,
      .autostart = autostart_set(),
      .report = s_report,
  };
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_READ
err_h sys_project_read(uint32_t offset, uint8_t* out, size_t len) {
  SE_CHECK_NOT_NULL(out);
  if (!s_has_active) SE_FAIL(ERR_PROJECT_NOT_STORED, 0);
  if (offset > s_active.length || len > s_active.length - offset) SE_FAIL(ERR_PROJECT_READ_RANGE, .offset = offset, .len = (uint32_t)len, .length = s_active.length);
  return read_gen(s_active.generation, s_active.length, offset, out, len);
}

// ---------------------------------------------------------------------------
// Boot replay and load
// ---------------------------------------------------------------------------

#undef OWNER
#define OWNER OWNER_SYS_PROJECT_REPLAY
err_h sys_project_replay(bool (*is_vm_frame)(const uint8_t* frame, size_t len)) {
  s_report = (sys_project_report_t){.state = SYS_PROJECT_REPLAY_NONE, .first_failed = UINT16_MAX};
  if (!s_has_active) return NULL;
  s_report.crc32 = s_active.crc32;
  if (s_active.schema_id != SE_schema_id()) {
    s_report.state = SYS_PROJECT_REPLAY_SKIPPED;
    SE_FAIL(ERR_PROJECT_SCHEMA, .stored = s_active.schema_id, .firmware = SE_schema_id());
  }
  uint32_t crc = 0;
  uint16_t count = 0;
  err_h err = crc_gen(s_active.generation, s_active.length, &crc);
  if (!err && crc != s_active.crc32) err = SE_ERR_NEW(ERR_PROJECT_CRC, .crc = crc, .expected = s_active.crc32);
  if (!err) err = check_frames(s_active.generation, s_active.length, NULL, &count);
  if (err) {
    s_report.state = SYS_PROJECT_REPLAY_CORRUPT;
    SE_TRY(err);
  }

  ESP_LOGI(TAG, "replaying stored code: %u frames", count);
  sys_interface_suspend_rx();
  uint32_t offset = 0;
  for (uint16_t index = 0; index < count; index++) {
    uint16_t frame_len = 0;
    err = read_gen(s_active.generation, s_active.length, offset, (uint8_t*)&frame_len, sizeof(frame_len));
    if (!err) err = read_gen(s_active.generation, s_active.length, offset + sizeof(frame_len), s_frame, frame_len);
    if (err) break;  // flash read failed: the rest can't be trusted
    offset += sizeof(frame_len) + frame_len;

    err_h frame_err = sys_interface_decode(s_frame, frame_len);
    if (!frame_err) {
      s_report.applied++;
      continue;
    }
    if (s_report.failed == 0) {
      s_report.first_failed = index;
      s_report.first_tag = (uint16_t)frame_err->tag;
      s_report.first_owner = (uint16_t)frame_err->owner;
    }
    s_report.failed++;
    if (is_vm_frame && is_vm_frame(s_frame, frame_len)) s_report.vm_failed = true;
    SE_REPORT(frame_err);
  }
  sys_interface_resume_rx();
  s_report.state = SYS_PROJECT_REPLAY_DONE;
  ESP_LOGI(TAG, "stored code replayed: %u applied, %u refused", s_report.applied, s_report.failed);
  SE_TRY(err);
  return NULL;
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
