#include "sys_device_slots_internal.h"

#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "utils.h"

_Static_assert(SYS_DEVICE_COUNT <= 64, "a dependency mask has one bit per slot");

/**
 * @brief One device of the registry. The slots are a static array indexed by the device ID, so a slot never moves
 * and its lock never goes away: a call that waits for the lock of a slot whose device was removed finds it not
 * installed. CREATE holds `lock` while create() runs and sets `installed` last, so a reader without `lock`
 * (sys_device_use, a sweep) never sees a half-made device. A device that takes calls is installed and not suspended.
 * Every write to `flags` is made with `lock` held and under the table lock (the flags share a byte, and the
 * dependency checks read them). The fields from `type` on are a device's and are cleared when it is removed; the ones
 * before it are the slot's and stay.
 */
typedef struct {
  StaticSemaphore_t lock_buf;  /**< Storage of `lock`, made once at boot */
  SemaphoreHandle_t lock;      /**< The device's own lock: not recursive, a second take by its holder is an error */
  uint16_t op;                 /**< The op `lock` was taken for; read without it for ERR_DEV_BUSY (a snapshot) */
  uint8_t gen;                 /**< Counts the devices the slot has had, a new one at the start of each CREATE
                                    (sys_device_slot_begin_create); under the table lock */
  const sys_device_type_t* type;  /**< The device's type; valid while `flags.installed` */
  void* instance;                 /**< The driver's struct; a remote device's sys_device_remote_t. Allocated by CREATE */
  sys_device_res_t* res;          /**< What the device took, the last added first (sys_device_res_add); under `lock` */
  uint64_t uses;                  /**< Bit i: this device uses the device in slot i (sys_device_use) */
  uint64_t used_by;               /**< Bit i: the device in slot i uses this one; REMOVE is refused while any */
  uint8_t in_flight;              /**< NOLOCK calls running without `lock`; under the table lock */
  struct {
    uint8_t installed : 1;  /**< The device exists: set when create() worked, cleared by REMOVE */
    uint8_t suspended : 1;  /**< The safe state: writes are refused until RESUME (an op's ALWAYS aside) */
    uint8_t system : 1;     /**< SYS_DEVICE_CREATE_SYSTEM: the board's own device, never removed */
    uint8_t remote : 1;     /**< On another board: `instance` is where, its calls go through the remote hook */
    uint8_t removing : 1;   /**< REMOVE found no user and goes on: no device can declare it uses this one */
  } flags;
} sys_device_slot_t;

static sys_device_slot_t s_slots[SYS_DEVICE_COUNT];  // zero at the start (.bss)

/* Guards `uses`, `used_by`, `gen`, `in_flight` and every write to `flags`. A critical section: a few bit operations,
 * nothing that waits, allocates or builds an error inside; taken after the lock of a slot. */
static portMUX_TYPE s_table_lock = portMUX_INITIALIZER_UNLOCKED;

static void table_lock(void) {
  portENTER_CRITICAL(&s_table_lock);
}

static void table_unlock(void) {
  portEXIT_CRITICAL(&s_table_lock);
}

/* The locks are made before app_main, like the static objects of utils.h (U_MUTEX_DEFINE). */
__attribute__((constructor)) static void slots_init(void) {
  for (uint32_t i = 0; i < SYS_DEVICE_COUNT; i++) {
    s_slots[i].lock = xSemaphoreCreateMutexStatic(&s_slots[i].lock_buf);
  }
}

static bool held_by_me(const sys_device_slot_t* s) {
  return xSemaphoreGetMutexHolder(s->lock) == xTaskGetCurrentTaskHandle();
}

static const sys_device_slot_t* slot_or_null(uint8_t id) {
  return id < SYS_DEVICE_COUNT ? &s_slots[id] : NULL;
}

err_h sys_device_slot_lock(uint8_t id, uint16_t op) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(!held_by_me(s), ERR_DEV_REENTRY, .id = id);
  const bool taken = xSemaphoreTake(s->lock, MSEC(CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS)) == pdTRUE;
  ERR_CHECK(taken, ERR_DEV_BUSY, .id = id, .op = s->op);  // the op of whoever holds it: a snapshot
  s->op = op;
  return NULL;
}

bool sys_device_slot_try_lock(uint8_t id, uint16_t op) {
  if (id >= SYS_DEVICE_COUNT || xSemaphoreTake(s_slots[id].lock, 0) != pdTRUE) {
    return false;  // held by another task, or by this one (not recursive: a take of its own fails at once)
  }
  s_slots[id].op = op;
  return true;
}

void sys_device_slot_unlock(uint8_t id) {
  if (id < SYS_DEVICE_COUNT) {
    (void)xSemaphoreGive(s_slots[id].lock);
  }
}

bool sys_device_slot_is_installed(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed;
}

bool sys_device_slot_is_suspended(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed && s->flags.suspended;
}

bool sys_device_slot_is_active(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed && !s->flags.suspended;
}

bool sys_device_slot_is_system(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed && s->flags.system;
}

bool sys_device_slot_is_remote(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed && s->flags.remote;
}

const sys_device_type_t* sys_device_slot_type(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed ? s->type : NULL;
}

void* sys_device_slot_instance(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL && s->flags.installed ? s->instance : NULL;
}

uint8_t sys_device_slot_gen(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL ? s->gen : 0;
}

uint64_t sys_device_slot_installed(void) {
  uint64_t mask = 0;
  table_lock();
  for (uint32_t i = 0; i < SYS_DEVICE_COUNT; i++) {
    if (s_slots[i].flags.installed) {
      mask |= 1ULL << i;
    }
  }
  table_unlock();
  return mask;
}

err_h sys_device_slot_begin_create(uint8_t id) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK(!s->flags.installed, ERR_DEV_ID_TAKEN, .id = id);
  table_lock();
  s->gen = s->gen == UINT8_MAX ? 1u : (uint8_t)(s->gen + 1u);  // never 0 for a device
  table_unlock();
  return NULL;
}

err_h sys_device_slot_install(uint8_t id, const sys_device_type_t* type, void* instance, bool system, bool remote) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK(!s->flags.installed, ERR_DEV_ID_TAKEN, .id = id);
  table_lock();
  s->type = type;
  s->instance = instance;
  s->flags.system = system;
  s->flags.remote = remote;
  s->flags.suspended = 0;
  s->flags.removing = 0;
  s->flags.installed = 1;  // last: a reader without the lock takes the slot only now
  table_unlock();
  return NULL;
}

bool sys_device_slot_begin_remove(uint8_t id, uint64_t* users) {
  if (id >= SYS_DEVICE_COUNT || !held_by_me(&s_slots[id])) {
    *users = 0;
    return false;
  }
  sys_device_slot_t* s = &s_slots[id];
  table_lock();
  *users = s->used_by;
  const bool free_of_users = s->flags.installed && s->used_by == 0;
  if (free_of_users) {
    s->flags.removing = 1;  // with the check, in one critical section with sys_device_slot_use
  }
  table_unlock();
  return free_of_users;
}

err_h sys_device_slot_uninstall(uint8_t id) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  table_lock();
  for (uint64_t m = s->uses; m != 0; m &= m - 1) {  // the devices this one used are free of it
    s_slots[__builtin_ctzll(m)].used_by &= ~(1ULL << id);
  }
  s->flags.installed = 0;  // first: a reader without the lock stops taking the slot
  memset(&s->type, 0, sizeof(*s) - offsetof(sys_device_slot_t, type));  // the device's fields; the slot's stay
  table_unlock();
  return NULL;
}

err_h sys_device_slot_set_suspended(uint8_t id, bool suspended) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK(s->flags.installed, ERR_DEV_NOT_FOUND, .id = id);
  table_lock();
  s->flags.suspended = suspended;
  table_unlock();
  return NULL;
}

err_h sys_device_slot_enter(uint8_t id) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK(s->flags.installed, ERR_DEV_NOT_FOUND, .id = id);
  table_lock();
  const bool full = s->in_flight == UINT8_MAX;
  if (!full) {
    s->in_flight++;
  }
  table_unlock();
  ERR_CHECK(!full, ERR_FULL, .max = UINT8_MAX);  // 255 calls of one device at once: a leak of leave()
  return NULL;
}

void sys_device_slot_leave(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT) {
    return;
  }
  table_lock();
  if (s_slots[id].in_flight > 0) {
    s_slots[id].in_flight--;
  }
  table_unlock();
}

static uint8_t in_flight_of(uint8_t id) {
  table_lock();
  const uint8_t n = s_slots[id].in_flight;
  table_unlock();
  return n;
}

void sys_device_slot_drain(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT) {
    return;
  }
  while (in_flight_of(id) != 0) {
    vTaskDelay(1);  // a NOLOCK call bounds its own time (SYS_DEVICE_OP_NOLOCK)
  }
}

err_h sys_device_slot_res_push(uint8_t id, sys_device_res_t* res) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  res->next = s->res;
  s->res = res;
  return NULL;
}

sys_device_res_t* sys_device_slot_res_pop(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT || !held_by_me(&s_slots[id])) {
    return NULL;
  }
  sys_device_res_t* res = s_slots[id].res;
  if (res != NULL) {
    s_slots[id].res = res->next;
    res->next = NULL;
  }
  return res;
}

// Whether device `from` uses device `to`, itself or through the devices it uses. The table lock is held: at most one
// step per slot, a few bit operations each.
static bool uses_reach(uint8_t from, uint8_t to) {
  uint64_t seen = 0;
  uint64_t todo = 1ULL << from;
  bool reached = false;
  while (todo != 0 && !reached) {
    const uint32_t i = (uint32_t)__builtin_ctzll(todo);
    todo &= todo - 1;
    seen |= 1ULL << i;
    reached = i == to;
    todo |= s_slots[i].uses & ~seen;
  }
  return reached;
}

err_h sys_device_slot_use(uint8_t user, uint8_t dep) {
  ERR_CHECK(user < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = user);
  ERR_CHECK(held_by_me(&s_slots[user]), ERR_DEV_NOT_LOCKED, .id = user);
  ERR_CHECK(dep < SYS_DEVICE_COUNT && dep != user, ERR_DEV_NOT_FOUND, .id = dep);
  table_lock();
  const bool exists = s_slots[dep].flags.installed && !s_slots[dep].flags.removing;
  const bool cycle = exists && uses_reach(dep, user);
  if (exists && !cycle) {
    s_slots[user].uses |= 1ULL << dep;
    s_slots[dep].used_by |= 1ULL << user;
  }
  table_unlock();
  ERR_CHECK(exists, ERR_DEV_NOT_FOUND, .id = dep);
  ERR_CHECK(!cycle, ERR_DEV_CYCLE, .id = user, .dep = dep);
  return NULL;
}

err_h sys_device_slot_unuse(uint8_t user, uint8_t dep) {
  ERR_CHECK(user < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = user);
  ERR_CHECK(dep < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = dep);
  ERR_CHECK(held_by_me(&s_slots[user]), ERR_DEV_NOT_LOCKED, .id = user);
  table_lock();
  s_slots[user].uses &= ~(1ULL << dep);
  s_slots[dep].used_by &= ~(1ULL << user);
  table_unlock();
  return NULL;
}

uint64_t sys_device_slot_uses(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT) {
    return 0;
  }
  table_lock();
  const uint64_t mask = s_slots[id].uses;
  table_unlock();
  return mask;
}

uint64_t sys_device_slot_used_by(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT) {
    return 0;
  }
  table_lock();
  const uint64_t mask = s_slots[id].used_by;
  table_unlock();
  return mask;
}
