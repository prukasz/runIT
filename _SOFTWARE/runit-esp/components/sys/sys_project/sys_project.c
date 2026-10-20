#include "sys_project.h"

#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "sys_actions_router.h"
#include "sys_device.h"
#include "sys_project_router.h"
#include "utils.h"

#define TAG __FILE_NAME__

#define NAMESPACE "project"
#define KEY_OPTS  "opts"
#define LEN_BYTES 2u                              // [u16 len] in front of every frame
#define FRAME_MAX (CONFIG_ROUTER_FRAME_MAX - 2u)  // keys + payload: a link frame without [seq][flags]
#define HDR       3u                              // [key][dir][code] in front of a command's args
#define RUN_FRAME (HDR + 4u)                      // [key][dir][code][u32 id]

// The project record runs the program's list, which runs a chunk: three levels of RUN.
_Static_assert(CONFIG_SYS_ACTIONS_MAX_DEPTH >= 3, "a project run nests three RUNs: raise SYS_ACTIONS_MAX_DEPTH");
_Static_assert(CONFIG_SYS_PROJECT_MAX_CHUNKS * (LEN_BYTES + RUN_FRAME) <= CONFIG_SYS_ACTIONS_MAX_BYTES,
               "the list of chunks must fit a record");
_Static_assert(CONFIG_SYS_PROJECT_MAX_SECTIONS * (LEN_BYTES + RUN_FRAME) + LEN_BYTES + HDR + sizeof(sys_project_mark_t)
                   <= CONFIG_SYS_ACTIONS_MAX_BYTES,
               "the project record must fit a record");

ERR_MODULE_DEFINE(ERR_MOD_SYS_PROJECT, "project", ERR_SYS_PROJECT_MAP)

typedef struct {
  uint32_t final_id;   // the ID the section's record gets at COMMIT
  uint32_t staged_id;  // where it waits until then
} section_t;

/* The one open save. The stream is parsed as it comes: a frame at a time into .frame, whole frames into .buf (the
 * record being built), which goes to flash when the section ends (or, for the program, when it is full). */
typedef struct {
  bool open;
  uint8_t flags;
  uint8_t bank;       // the one being filled
  uint16_t seq;       // the IDs given so far in this bank
  uint32_t want, got, crc;
  // parser
  uint8_t len_got;    // bytes of flen so far: 0 only between frames
  uint16_t flen, fgot;
  // section
  bool in_section, is_program, program_seen;
  uint32_t final_id;
  uint16_t buf_len;
  uint16_t sections;
  uint16_t chunks_len;  // bytes of .chunks
} save_t;

U_MUTEX_DEFINE(sys_project_mutex);  // s_project but .nvs: both router tasks write, ops open / close
#define LOCK()   ((void)U_MUTEX_LOCK(sys_project_mutex, WAIT_FOREVER))
#define UNLOCK() ((void)U_MUTEX_UNLOCK(sys_project_mutex))

static struct {
  save_t save;                                                              // zeroed whole when a save closes
  uint8_t buf[CONFIG_SYS_ACTIONS_MAX_BYTES];                                // the record being built
  uint8_t frame[FRAME_MAX];                                                 // the frame being parsed
  uint8_t chunks[CONFIG_SYS_PROJECT_MAX_CHUNKS * (LEN_BYTES + RUN_FRAME)];  // the program's list of chunks
  section_t secs[CONFIG_SYS_PROJECT_MAX_SECTIONS];                          // the sections staged so far
  nvs_handle_t nvs;                                                         // the options; opened at init
} s_project;  // zero at the start (.bss)

/* ----------------------------------------------------------------------------
 * IDs one after another
 * ------------------------------------------------------------------------- */

// The next ID of the bank being filled: 1, 2, 3... (0 is the program's list of chunks).
static uint32_t next_id(void) {
  return SYS_PROJECT_ID(s_project.save.bank, ++s_project.save.seq);
}

// Writes [u16 len][key][write][op][args] at `at`: a frame as a record holds it. Returns the bytes written.
static uint16_t put_frame(uint8_t* at, uint8_t key, uint8_t op, const void* args, uint16_t args_len) {
  const uint16_t flen = HDR + args_len;
  memcpy(at, &flen, LEN_BYTES);
  at[LEN_BYTES] = key;
  at[LEN_BYTES + 1] = CMD_DIR_WRITE;
  at[LEN_BYTES + 2] = op;
  memcpy(at + LEN_BYTES + HDR, args, args_len);
  return LEN_BYTES + flen;
}

// Removes every record of a bank (what a failed save left, or what a stored project no longer needs).
static err_h clear_bank(uint8_t bank) {
  const uint32_t lo = SYS_PROJECT_ID(bank, 0);
  const uint32_t hi = SYS_PROJECT_ID(bank, 0xFFFFu);
  uint32_t ids[8];
  for (;;) {
    uint16_t count = 0;
    ERR_TRY(sys_actions_list(lo, ids, ARRAY_LEN(ids), &count, NULL));
    uint16_t n = 0;
    while (n < count && ids[n] <= hi) {
      ERR_TRY(sys_actions_remove(ids[n]));
      n++;
    }
    if (count < ARRAY_LEN(ids) || n < count) {
      return NULL;
    }
  }
}

/* ----------------------------------------------------------------------------
 * The stored project: its record starts with a MARK frame
 * ------------------------------------------------------------------------- */

static err_h mark_frame(const uint8_t* frame, uint16_t len, bool last, void* ctx) {
  sys_project_mark_t* mark = ctx;
  if (len == HDR + sizeof(*mark) && frame[0] == ROUTER_KEY_PROJECT && frame[1] == CMD_DIR_WRITE &&
      frame[2] == SYS_PROJECT_OP_MARK && mark->bank == 0xFFu) {  // the first one only
    memcpy(mark, frame + HDR, sizeof(*mark));
  }
  return NULL;
}

// The project record's MARK; false when no project is stored (or its record is damaged).
static bool read_mark(sys_project_mark_t* mark) {
  mark->bank = 0xFFu;
  err_h err = sys_actions_dump(SYS_ACTIONS_ID_PROJECT, mark_frame, mark);
  if (err != NULL) {
    if (err->tag != ERR_ACTION_NOT_FOUND) {
      err_report(err);
    } else {
      err_free(err);
    }
    return false;
  }
  return mark->bank < SYS_PROJECT_BANKS;
}

/* ----------------------------------------------------------------------------
 * Options
 * ------------------------------------------------------------------------- */

bool sys_project_autostart(void) {
  uint8_t on = 0;
  return nvs_get_u8(s_project.nvs, KEY_OPTS, &on) == ESP_OK && on != 0;
}

err_h sys_project_set_autostart(bool on) {
  ERR_TRY_ESP(nvs_set_u8(s_project.nvs, KEY_OPTS, on ? 1 : 0));
  return ERR_ESP(nvs_commit(s_project.nvs));
}

err_h sys_project_init(void) {
  // sys_actions_init opened the partition; the options are one more namespace in it.
  return ERR_ESP(nvs_open_from_partition(CONFIG_SYS_ACTIONS_PARTITION, NAMESPACE, NVS_READWRITE, &s_project.nvs));
}

/* ----------------------------------------------------------------------------
 * Sections (lock held)
 * ------------------------------------------------------------------------- */

static err_h flush_chunk(void) {
  ERR_CHECK(s_project.save.chunks_len + LEN_BYTES + RUN_FRAME <= sizeof(s_project.chunks), ERR_PROJECT_CHUNKS,
            .max = CONFIG_SYS_PROJECT_MAX_CHUNKS);
  const uint32_t id = next_id();
  ERR_TRY(sys_actions_set(id, s_project.buf, s_project.save.buf_len));
  uint8_t* at = s_project.chunks + s_project.save.chunks_len;
  s_project.save.chunks_len += put_frame(at, ROUTER_KEY_ACTIONS, SYS_ACTIONS_OP_RUN, &id, sizeof(id));
  s_project.save.buf_len = 0;
  return NULL;
}

static err_h section_close(void) {
  if (!s_project.save.in_section) {
    return NULL;
  }
  ERR_CHECK(s_project.save.buf_len > 0, ERR_PROJECT_SECTION_EMPTY, .id = s_project.save.final_id);
  if (s_project.save.is_program) {
    ERR_TRY(flush_chunk());
  } else {
    const uint32_t staged = next_id();
    ERR_TRY(sys_actions_set(staged, s_project.buf, s_project.save.buf_len));
    s_project.secs[s_project.save.sections++] = (section_t){.final_id = s_project.save.final_id, .staged_id = staged};
  }
  s_project.save.in_section = false;
  s_project.save.buf_len = 0;
  return NULL;
}

static err_h section_open(uint32_t id) {
  ERR_TRY(section_close());
  const bool program = id == SYS_PROJECT_SECTION_PROGRAM;
  ERR_CHECK(program || (id >> 24) <= SYS_ACTIONS_CLASS_DEVICE, ERR_PROJECT_BAD_SECTION, .id = id);
  ERR_CHECK(!s_project.save.program_seen, ERR_PROJECT_ORDER);  // the program is the last section
  if (program) {
    s_project.save.program_seen = true;
  } else {
    ERR_CHECK(s_project.save.sections < CONFIG_SYS_PROJECT_MAX_SECTIONS, ERR_PROJECT_SECTION_FULL,
              .max = CONFIG_SYS_PROJECT_MAX_SECTIONS);
    for (uint16_t i = 0; i < s_project.save.sections; i++) {
      ERR_CHECK(s_project.secs[i].final_id != id, ERR_PROJECT_SECTION_DUP, .id = id);
    }
  }
  s_project.save.in_section = true;
  s_project.save.is_program = program;
  s_project.save.final_id = id;
  s_project.save.buf_len = 0;
  return NULL;
}

// A frame that is not a SECTION: it belongs to the open section, whole.
static err_h frame_add(void) {
  ERR_CHECK(s_project.save.in_section, ERR_PROJECT_NO_SECTION);
  const uint32_t need = LEN_BYTES + s_project.save.flen;
  const uint32_t max = sys_actions_max_bytes(s_project.save.final_id);  // the program takes the user class's limit
  if (s_project.save.buf_len + need > max) {
    ERR_CHECK(s_project.save.is_program && s_project.save.buf_len > 0, ERR_PROJECT_SECTION_BIG,
              .id = s_project.save.final_id, .max = max);
    ERR_TRY(flush_chunk());
  }
  memcpy(s_project.buf + s_project.save.buf_len, &s_project.save.flen, LEN_BYTES);
  memcpy(s_project.buf + s_project.save.buf_len + LEN_BYTES, s_project.frame, s_project.save.flen);
  s_project.save.buf_len += (uint16_t)need;
  return NULL;
}

static err_h frame_done(void) {
  if (s_project.save.flen >= HDR && s_project.frame[0] == ROUTER_KEY_PROJECT && s_project.frame[1] == CMD_DIR_WRITE &&
      s_project.frame[2] == SYS_PROJECT_OP_SECTION) {
    uint32_t id = 0;
    ERR_CHECK(s_project.save.flen == HDR + sizeof(id), ERR_PROJECT_BAD_SECTION, .id = 0);
    memcpy(&id, s_project.frame + HDR, sizeof(id));
    return section_open(id);
  }
  return frame_add();
}

// The stream's next bytes through the parser: [u16 len][frame] back to back, in any cut.
static err_h feed(const uint8_t* p, uint32_t n, uint32_t offset) {
  while (n > 0) {
    if (s_project.save.len_got < LEN_BYTES) {
      ((uint8_t*)&s_project.save.flen)[s_project.save.len_got++] = *p++;  // little-endian, as the stream
      n--;
      offset++;
      if (s_project.save.len_got == LEN_BYTES) {
        s_project.save.fgot = 0;
        ERR_CHECK(s_project.save.flen >= 1 && s_project.save.flen <= FRAME_MAX, ERR_PROJECT_BAD_FRAME,
                  .offset = offset - LEN_BYTES);
      }
      continue;
    }
    uint32_t take = s_project.save.flen - s_project.save.fgot;
    if (take > n) {
      take = n;
    }
    memcpy(s_project.frame + s_project.save.fgot, p, take);
    s_project.save.fgot += (uint16_t)take;
    p += take;
    n -= take;
    offset += take;
    if (s_project.save.fgot == s_project.save.flen) {
      s_project.save.len_got = 0;  // a frame is complete: only a cut inside one leaves len_got set at COMMIT
      ERR_TRY(frame_done());
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Save
 * ------------------------------------------------------------------------- */

// Lock held: drops the save and the records it staged.
static void save_drop(void) {
  if (s_project.save.open) {
    err_report(clear_bank(s_project.save.bank));
  }
  s_project.save = (save_t){0};
}

err_h sys_project_begin(uint32_t len, uint8_t flags) {
  ERR_CHECK(len > 0 && len <= CONFIG_SYS_PROJECT_MAX_BYTES, ERR_PROJECT_TOO_BIG, .len = len,
            .max = CONFIG_SYS_PROJECT_MAX_BYTES);
  LOCK();
  err_h err = NULL;
  if (s_project.save.open) {
    err = ERR_NEW(ERR_PROJECT_BUSY);
  } else {
    sys_project_mark_t mark;
    const uint8_t bank = read_mark(&mark) ? (uint8_t)(mark.bank ^ 1u) : 0u;  // the one the stored project doesn't use
    err = clear_bank(bank);
    if (err == NULL) {
      s_project.save = (save_t){.open = true, .flags = flags, .bank = bank, .want = len};
    }
  }
  UNLOCK();
  return err;
}

err_h sys_project_write(uint32_t offset, const uint8_t* data, uint32_t len) {
  ERR_CHECK(data != NULL && len > 0, ERR_PROJECT_BAD_ARG);
  LOCK();
  err_h err = NULL;
  if (!s_project.save.open) {
    err = ERR_NEW(ERR_PROJECT_NO_SESSION);
  } else if (offset != s_project.save.got) {
    err = ERR_NEW(ERR_PROJECT_OFFSET, .got = offset, .want = s_project.save.got);  // the save goes on: a resend
  } else if (len > s_project.save.want - s_project.save.got) {
    err = ERR_NEW(ERR_PROJECT_TOO_BIG, .len = s_project.save.got + len, .max = s_project.save.want);
    save_drop();
  } else {
    s_project.save.crc = esp_rom_crc32_le(s_project.save.crc, data, len);
    err = feed(data, len, offset);
    s_project.save.got += len;
    if (err != NULL) {
      save_drop();
    }
  }
  UNLOCK();
  return err;
}

// Lock held, every byte in and checked: the project record (MARK, the sections' RUN frames, the program) becomes the
// stored one. Returns after the old bank is cleared.
static err_h commit_store(void) {
  ERR_TRY(section_close());
  ERR_CHECK(s_project.save.sections > 0 || s_project.save.program_seen, ERR_PROJECT_EMPTY);

  // the program's list of chunks: staged like the rest
  const uint32_t list_id = SYS_PROJECT_ID(s_project.save.bank, 0);
  if (s_project.save.program_seen) {
    ERR_TRY(sys_actions_set(list_id, s_project.chunks, s_project.save.chunks_len));
  }
  // the settings and device records: from staging to their IDs
  for (uint16_t i = 0; i < s_project.save.sections; i++) {
    ERR_TRY(sys_actions_copy(s_project.secs[i].staged_id, s_project.secs[i].final_id));
    ERR_TRY(sys_actions_remove(s_project.secs[i].staged_id));
  }
  // the project record: the commit point
  const sys_project_mark_t mark = {
      .bank = s_project.save.bank, .crc32 = s_project.save.crc, .bytes = s_project.save.want};
  uint16_t len = put_frame(s_project.buf, ROUTER_KEY_PROJECT, SYS_PROJECT_OP_MARK, &mark, sizeof(mark));
  for (uint16_t i = 0; i < s_project.save.sections; i++) {
    const uint32_t id = s_project.secs[i].final_id;
    len += put_frame(s_project.buf + len, ROUTER_KEY_ACTIONS, SYS_ACTIONS_OP_RUN_ALL, &id, sizeof(id));
  }
  if (s_project.save.program_seen) {  // last; RUN, not RUN_ALL: the program stops at a failing frame
    len += put_frame(s_project.buf + len, ROUTER_KEY_ACTIONS, SYS_ACTIONS_OP_RUN, &list_id, sizeof(list_id));
  }
  ERR_TRY(sys_actions_set(SYS_ACTIONS_ID_PROJECT, s_project.buf, len));
  // the old project's program records are not needed any more
  err_report(clear_bank(s_project.save.bank ^ 1u));
  return NULL;
}

err_h sys_project_commit(uint32_t crc32) {
  LOCK();
  err_h err;
  bool run = false;
  if (!s_project.save.open) {
    err = ERR_NEW(ERR_PROJECT_NO_SESSION);
  } else if (s_project.save.got != s_project.save.want) {
    err = ERR_NEW(ERR_PROJECT_INCOMPLETE, .got = s_project.save.got, .want = s_project.save.want);
  } else if (s_project.save.crc != crc32) {
    err = ERR_NEW(ERR_PROJECT_CRC, .got = crc32, .want = s_project.save.crc);
  } else if (s_project.save.len_got != 0) {
    err = ERR_NEW(ERR_PROJECT_TRUNCATED);
  } else {
    err = commit_store();
    if (err == NULL) {
      err = sys_project_set_autostart((s_project.save.flags & SYS_PROJECT_FLAG_AUTOSTART) != 0);
      run = (s_project.save.flags & SYS_PROJECT_FLAG_RUN) != 0;
    }
  }
  if (err == NULL) {
    s_project.save = (save_t){0};  // stored: the bank is the project's now
  } else {
    save_drop();
  }
  UNLOCK();
  if (err == NULL && run) {
    err = sys_project_run();
    if (err != NULL) {
      err = ERR_WRAP(err, ERR_PROJECT_RUN);
    }
  }
  return err;
}

void sys_project_abort(void) {
  LOCK();
  save_drop();
  UNLOCK();
}

/* ----------------------------------------------------------------------------
 * Run, info, erase, boot
 * ------------------------------------------------------------------------- */

err_h sys_project_info(sys_project_info_t* out) {
  ERR_CHECK(out != NULL, ERR_PROJECT_BAD_ARG);
  sys_project_mark_t mark;
  *out = (sys_project_info_t){.autostart = sys_project_autostart()};
  if (read_mark(&mark)) {
    out->stored = 1;
    out->bank = mark.bank;
    out->crc32 = mark.crc32;
    out->bytes = mark.bytes;
  }
  return NULL;
}

err_h sys_project_run(void) {
  sys_project_mark_t mark;
  ERR_CHECK(read_mark(&mark), ERR_PROJECT_NOT_STORED);
  return sys_actions_run(SYS_ACTIONS_ID_PROJECT, true);
}

err_h sys_project_erase(void) {
  LOCK();
  err_h err = s_project.save.open ? ERR_NEW(ERR_PROJECT_BUSY) : NULL;
  if (err == NULL) {
    // the project's devices, newest first; the board's stay. A failed uninstall still frees its device: go on.
    err_report(sys_device_sweep(SYS_DEVICE_OP_REMOVE));
    err = sys_actions_remove(SYS_ACTIONS_ID_PROJECT);  // first: without it the banks are not a project
    if (err != NULL && err->tag == ERR_ACTION_NOT_FOUND) {
      err_free(err);
      err = NULL;
    }
  }
  for (uint8_t bank = 0; err == NULL && bank < SYS_PROJECT_BANKS; bank++) {
    err = clear_bank(bank);
  }
  UNLOCK();
  return err;
}

err_h sys_project_boot(void) {
  sys_project_info_t info;
  ERR_TRY(sys_project_info(&info));
  if (!info.stored || !info.autostart) {
    return NULL;
  }
  ESP_LOGI(TAG, "autostart: running the stored project");
  return router_inject((const uint8_t[]){ROUTER_KEY_PROJECT, CMD_DIR_WRITE, SYS_PROJECT_OP_RUN}, HDR, false);
}
