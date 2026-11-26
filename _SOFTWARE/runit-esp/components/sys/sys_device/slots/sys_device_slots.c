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
 * `flags` and `gen` are written by the holder of `lock` only, with atomic stores (release), and read without a lock
 * (acquire): what a reader sees installed has its `type` and `instance`. `removing` is set under the table lock as
 * well, in one critical section with the check of `used_by`. The fields from `type` on are a device's and are cleared
 * when it is removed; the ones before it are the slot's and stay.
 */
typedef struct {
  StaticSemaphore_t lock_buf;  /**< Storage of `lock`, made once at boot */
  SemaphoreHandle_t lock;      /**< The device's own lock: not recursive, a second take by its holder is an error */
  uint16_t op;                 /**< The op `lock` was taken for; read without it for ERR_DEV_BUSY (a snapshot) */
  uint8_t gen;                 /**< Counts the devices the slot has had, a new one at the start of each CREATE
                                    (sys_device_slot_begin_create); atomics only */
  const sys_device_type_t* type;  /**< The device's type; valid while F_INSTALLED */
  void* instance;                 /**< The driver's struct; a remote device's sys_device_remote_t. Allocated by CREATE */
  sys_device_res_t* res;          /**< What the device took, the last added first (sys_device_res_add); under `lock` */
  uint64_t uses;                  /**< Bit i: this device uses the device in slot i (sys_device_use) */
  uint64_t used_by;               /**< Bit i: the device in slot i uses this one; REMOVE is refused while any */
  uint32_t in_flight;             /**< NOLOCK calls running without `lock`, at most UINT8_MAX: counted up under `lock`,
                                       down by any task; atomics only */
  uint8_t flags;                  /**< F_* bits; atomics only */
} sys_device_slot_t;

#define F_INSTALLED 0x01u /**< The device exists: set when create() worked, cleared by REMOVE */
#define F_SUSPENDED 0x02u /**< The safe state: writes are refused until RESUME (an op's ALWAYS aside) */
#define F_SYSTEM    0x04u /**< SYS_DEVICE_CREATE_SYSTEM: the board's own device, never removed */
#define F_REMOTE    0x08u /**< On another board: `instance` is where, its calls go through the remote hook */
#define F_REMOVING  0x10u /**< REMOVE found no user and goes on: no device can declare it uses this one */

static sys_device_slot_t s_slots[SYS_DEVICE_COUNT];  // zero at the start (.bss)

/* Guards `uses`, `used_by` and the setting of `removing`. A critical section: a few bit operations, nothing that
 * waits, allocates or builds an error inside; taken after the lock of a slot. */
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

// The flags of slot `id`, 0 past the table. Acquire: a slot seen installed has its type and instance.
static uint8_t flags_of(uint8_t id) {
  return id < SYS_DEVICE_COUNT ? __atomic_load_n(&s_slots[id].flags, __ATOMIC_ACQUIRE) : 0u;
}

// Only the holder of the slot's lock changes its flags, so its own read of them and the store make no race.
static void flags_set(sys_device_slot_t* s, uint8_t set, uint8_t clear) {
  __atomic_store_n(&s->flags, (uint8_t)((s->flags & ~clear) | set), __ATOMIC_RELEASE);
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
  return (flags_of(id) & F_INSTALLED) != 0u;
}

bool sys_device_slot_is_suspended(uint8_t id) {
  return (flags_of(id) & (F_INSTALLED | F_SUSPENDED)) == (F_INSTALLED | F_SUSPENDED);
}

bool sys_device_slot_is_active(uint8_t id) {
  return (flags_of(id) & (F_INSTALLED | F_SUSPENDED)) == F_INSTALLED;
}

bool sys_device_slot_is_system(uint8_t id) {
  return (flags_of(id) & (F_INSTALLED | F_SYSTEM)) == (F_INSTALLED | F_SYSTEM);
}

bool sys_device_slot_is_remote(uint8_t id) {
  return (flags_of(id) & (F_INSTALLED | F_REMOTE)) == (F_INSTALLED | F_REMOTE);
}

const sys_device_type_t* sys_device_slot_type(uint8_t id) {
  return (flags_of(id) & F_INSTALLED) != 0u ? s_slots[id].type : NULL;
}

void* sys_device_slot_instance(uint8_t id) {
  return (flags_of(id) & F_INSTALLED) != 0u ? s_slots[id].instance : NULL;
}

uint8_t sys_device_slot_gen(uint8_t id) {
  const sys_device_slot_t* s = slot_or_null(id);
  return s != NULL ? __atomic_load_n(&s->gen, __ATOMIC_ACQUIRE) : 0;
}

uint64_t sys_device_slot_installed(void) {
  uint64_t mask = 0;
  for (uint32_t i = 0; i < SYS_DEVICE_COUNT; i++) {  // a snapshot, slot by slot
    if ((flags_of((uint8_t)i) & F_INSTALLED) != 0u) {
      mask |= 1ULL << i;
    }
  }
  return mask;
}

err_h sys_device_slot_begin_create(uint8_t id) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK((s->flags & F_INSTALLED) == 0u, ERR_DEV_ID_TAKEN, .id = id);
  // Only the lock's holder writes it: its own read and the store make no race. Never 0 for a device.
  __atomic_store_n(&s->gen, s->gen == UINT8_MAX ? 1u : (uint8_t)(s->gen + 1u), __ATOMIC_RELEASE);
  return NULL;
}

err_h sys_device_slot_install(uint8_t id, const sys_device_type_t* type, void* instance, bool system, bool remote) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK((s->flags & F_INSTALLED) == 0u, ERR_DEV_ID_TAKEN, .id = id);
  s->type = type;
  s->instance = instance;
  // Last, with release: a reader without the lock takes the slot only now.
  flags_set(s, (uint8_t)(F_INSTALLED | (system ? F_SYSTEM : 0u) | (remote ? F_REMOTE : 0u)), UINT8_MAX);
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
  const bool free_of_users = (s->flags & F_INSTALLED) != 0u && s->used_by == 0;
  if (free_of_users) {
    flags_set(s, F_REMOVING, 0u);  // with the check, in one critical section with sys_device_slot_use
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
  flags_set(s, 0u, UINT8_MAX);  // first: a reader without the lock stops taking the slot
  // The device's fields; the slot's stay. `in_flight` is 0 (drained) and `flags` cleared just now: not in the memset.
  memset(&s->type, 0, offsetof(sys_device_slot_t, in_flight) - offsetof(sys_device_slot_t, type));
  table_unlock();
  return NULL;
}

err_h sys_device_slot_set_suspended(uint8_t id, bool suspended) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK((s->flags & F_INSTALLED) != 0u, ERR_DEV_NOT_FOUND, .id = id);
  flags_set(s, suspended ? F_SUSPENDED : 0u, suspended ? 0u : F_SUSPENDED);
  return NULL;
}

err_h sys_device_slot_enter(uint8_t id) {
  ERR_CHECK(id < SYS_DEVICE_COUNT, ERR_DEV_BAD_ID, .id = id);
  sys_device_slot_t* s = &s_slots[id];
  ERR_CHECK(held_by_me(s), ERR_DEV_NOT_LOCKED, .id = id);
  ERR_CHECK((s->flags & F_INSTALLED) != 0u, ERR_DEV_NOT_FOUND, .id = id);
  // Only the lock's holder counts up (this one), so the count cannot pass the limit between the check and the add.
  const bool full = __atomic_load_n(&s->in_flight, __ATOMIC_RELAXED) >= UINT8_MAX;
  if (!full) {
    (void)__atomic_add_fetch(&s->in_flight, 1u, __ATOMIC_RELAXED);
  }
  ERR_CHECK(!full, ERR_FULL, .max = UINT8_MAX);  // 255 calls of one device at once: a leak of leave()
  return NULL;
}

void sys_device_slot_leave(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT) {
    return;
  }
  if (__atomic_load_n(&s_slots[id].in_flight, __ATOMIC_RELAXED) != 0u) {
    (void)__atomic_sub_fetch(&s_slots[id].in_flight, 1u, __ATOMIC_RELEASE);  // what the call did, before drain sees it
  }
}

void sys_device_slot_drain(uint8_t id) {
  if (id >= SYS_DEVICE_COUNT) {
    return;
  }
  while (__atomic_load_n(&s_slots[id].in_flight, __ATOMIC_ACQUIRE) != 0u) {
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
  const bool exists = (flags_of(dep) & (F_INSTALLED | F_REMOVING)) == F_INSTALLED;
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
