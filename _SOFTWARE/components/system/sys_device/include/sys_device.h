#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "status.h"

#define MAX_DEVICE_ID 127

#define SYS_DEV_CHECK_HANDLE_R(handle, dev_id)                                                            \
  do {                                                                                                    \
    if ((handle) == NULL) {                                                                               \
      ESP_LOGE(__FILE_NAME__, "%s: Pointer '%s' is NULL", __func__, #handle);                             \
      return STA_C(ERR_DEV_MISSING_HANDLE, OWNER, DEV_ERR_PACK((dev_id), 0, 0), STATUS_PAYLOAD_DEV_SOLO); \
    }                                                                                                     \
  } while (0)

#define SYS_DEV_CHECK_HANDLE_RP(handle, dev_id)                                                                             \
  do {                                                                                                                      \
    if ((handle) == NULL) {                                                                                                 \
      ESP_LOGE(__FILE_NAME__, "%s: Pointer '%s' is NULL", __func__, #handle);                                               \
      status_rep_t __sta_err = STA_C(ERR_DEV_MISSING_HANDLE, OWNER, DEV_ERR_PACK((dev_id), 0, 0), STATUS_PAYLOAD_DEV_SOLO); \
      STA_P(__sta_err);                                                                                                     \
      return __sta_err;                                                                                                     \
    }                                                                                                                       \
  } while (0)

#define SYS_DEV_STA_W(code, owner, info) STA_W((code), (owner), (info), STATUS_PAYLOAD_DEV_ESP)
#define SYS_DEV_STA_C(code, owner, info) STA_C((code), (owner), (info), STATUS_PAYLOAD_DEV_ESP)
#define SYS_DEV_STA_I(code, owner, info) STA_I((code), (owner), (info), STATUS_PAYLOAD_DEV_ESP)

#define SYS_DEV_CHECK_NOT_NULL_R(ptr) CHECK_NOT_NULL_X((ptr), 1, 0, 0, STATUS_PAYLOAD_DEV_ESP)
#define SYS_DEV_CHECK_NOT_NULL_RP(ptr) CHECK_NOT_NULL_X((ptr), 1, 1, 0, STATUS_PAYLOAD_DEV_ESP)

#define SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx)                                                                                                    \
  do {                                                                                                                                                 \
    esp_err_t __err = (driver_call);                                                                                                                   \
    if (__err != ESP_OK) {                                                                                                                             \
      ESP_LOGE(__FILE_NAME__, "%s: ESP API Failed '%s' -> %s, device id %d", __func__, #driver_call, esp_err_to_name(__err), ((ctx))->base.device_id); \
      return SYS_DEV_STA_C(ERR_DEV_DRIVER_ERR, OWNER, DEV_ERR_PACK(((ctx))->base.device_id, 0, __err));                                                \
    }                                                                                                                                                  \
  } while (0)

#define SYS_DEV_CHECK_DEP_CALL(status_call, ctx, dep_dev_id)                                                                                                                                                  \
  do {                                                                                                                                                                                                        \
    status_rep_t __status = (status_call);                                                                                                                                                                    \
    if (STA_IS_ERR(__status)) {                                                                                                                                                                               \
      ESP_LOGE(__FILE_NAME__, "%s: Dependency Call Failed '%s' -> %s, device id %d, dep device id %d", __func__, #status_call, status_error_to_name(__status.e_code), ((ctx))->base.device_id, (dep_dev_id)); \
      return STA_C(ERR_DEV_DEP_ERR, OWNER, DEV_ERR_PACK(((ctx))->base.device_id, (dep_dev_id), __status.e_code), STATUS_PAYLOAD_DEV_DEP);                                                                     \
    }                                                                                                                                                                                                         \
  } while (0)

typedef enum sys_device_role_t { SYS_DEV_ROLE_DEF, SYS_DEV_ROLE_IO, SYS_DEV_ROLE_PWR, SYS_DEV_ROLE_USER } sys_device_role_e;

typedef struct {
  uint16_t none : 1;
  uint16_t emergency_action : 1;
  uint16_t vm_action : 1;
  uint16_t _padding : 13;
} callback_route_t;

typedef struct sys_device_t {
  uint8_t device_id;

  sys_device_role_e role;
  void* device_handle;
  const char* name;
  void** install_args;
  bool is_suspended;
  bool is_installed;
  struct {
    uint8_t suspend_on : 2;
    uint8_t reset_on : 2;
    uint8_t uinstall_on : 2;
    uint8_t emergency_action_on : 2;
  } err_cfg;

  status_rep_t (*install_device)(void** install_args, void** out_device_handle);
  status_rep_t (*error_handler)(void* device_handle, status_rep_t* error);

  status_rep_t (*uninstall_device)(void* device_handle);
  status_rep_t (*reset_device)(void* device_handle);
  status_rep_t (*suspend_device)(void* device_handle);
  status_rep_t (*resume_device)(void* device_handle);
  status_rep_t (*freeze_device)(void* device_handle);
  status_rep_t (*sync_device)(void* device_handle);
  void* contracts[4];  // 0: IO, 1: POWER_VREG, 2: POWER_MONITOR, 3: POWER_USB_PD
} sys_device_t;

typedef enum { SYS_DEVICE_CONTRACT_IO = 0, SYS_DEVICE_CONTRACT_POWER_VREG = 1, SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, SYS_DEVICE_CONTRACT_MAX = 4 } sys_device_contract_type_e;

#define SYS_DEV_GET_CONTRACT(dev, type) ((dev) ? (dev)->contracts[(type)] : NULL)

#define IF_SYS_DEV_AND_FEATURE(device_id, contract_type, contract_struct_type, func_member, dev_ptr, vtable_ptr)                                            \
  for (sys_device_t* dev_ptr = sys_device_get_by_id((device_id)); dev_ptr; dev_ptr = NULL)                                                                  \
    for (const contract_struct_type* vtable_ptr = (const contract_struct_type*)SYS_DEV_GET_CONTRACT(dev_ptr, contract_type); vtable_ptr; vtable_ptr = NULL) \
      if (dev_ptr->is_installed && !dev_ptr->is_suspended && vtable_ptr->func_member)

#define SYS_DEV_DISPATCH(dev_id, contract_enum, contract_type, func_name, ...)                       \
  do {                                                                                               \
    sys_device_t* __disp_dev = sys_device_get_by_id((dev_id));                                       \
    if (__disp_dev == NULL) {                                                                        \
      STA_RP(SYS_DEV_STA_W(ERR_DEV_NOT_FOUND, OWNER, DEV_ERR_PACK((dev_id), 0, 0)));                 \
    }                                                                                                \
    if (!__disp_dev->is_installed) {                                                                 \
      STA_RP(SYS_DEV_STA_W(ERR_DEV_NOT_INSTALLED, OWNER, DEV_ERR_PACK((dev_id), 0, 0)));             \
    }                                                                                                \
    if (__disp_dev->is_suspended) {                                                                  \
      STA_RP(SYS_DEV_STA_W(ERR_DEV_SUSPENDED, OWNER, DEV_ERR_PACK((dev_id), 0, 0)));                 \
    }                                                                                                \
    IF_SYS_DEV_AND_FEATURE((dev_id), contract_enum, contract_type, func_name, dev_ptr, vtable_ptr) { \
      return vtable_ptr->func_name(dev_ptr->device_handle, ##__VA_ARGS__);                           \
    }                                                                                                \
    STA_RP(SYS_DEV_STA_W(ERR_NOT_SUPPORTED, OWNER, DEV_ERR_PACK((dev_id), 0, 0)));                   \
  } while (0)

#define SYS_DEV_ARG_PACK(val) ((void*)(uintptr_t)(val))
#define SYS_DEV_ARG_UNPACK(type, var, args, index) type var = (type)(uintptr_t)(args)[index]
#define SYS_DEV_ARG_UNPACK_VAL(type, args, index) ((type)(uintptr_t)(args)[index])

#define SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle) \
  ctx_type* ctx_var = (ctx_type*)(input_handle);                                      \
  SYS_DEV_CHECK_HANDLE_R(ctx_var, 0);                                                 \
  hw_type hw_var = (hw_type)(((ctx_var))->base.hw_handle);                            \
  SYS_DEV_CHECK_HANDLE_R(hw_var, ((ctx_var))->base.device_id)

#define IF_SYS_DEV_FROZEN(ctx) if (((ctx))->base.is_frozen)
#define SYS_DEV_CTX_FREEZE(ctx) ((ctx))->base.is_frozen = true
#define SYS_DEV_CTX_UNFREEZE(ctx) ((ctx))->base.is_frozen = false

typedef struct {
  void* hw_handle;
  uint8_t device_id;
  bool is_frozen;
} sys_device_adapter_base_t;

void* sys_device_allocate_ctx(size_t total_size, void** install_args);

/**
 * @brief Create new device, after run new device will be automatically
 * installed via provided "install_device" function
 */
status_rep_t sys_device_install(sys_device_t* device);

/**
 * @brief Uninstall created device, device will be completely removed
 */
status_rep_t sys_device_uninstall(uint8_t device_id);

/**
 * @brief Run privided reset function
 */
status_rep_t sys_device_reset(uint8_t device_id);

/**
 * @brief suspend and save current state
 */
status_rep_t sys_device_suspend(uint8_t device_id);

/**
 * @brief resume from saved state
 */
status_rep_t sys_device_resume(uint8_t device_id);

/**
 * @brief suspend all devices
 */
status_rep_t sys_device_suspend_all(void);

/**
 * @brief resume all devices
 */
status_rep_t sys_device_resume_all(void);

/**
 * @brief freeze device updates and save actions in internal driver state
 */
status_rep_t sys_device_freeze(uint8_t device_id);

/**
 * @brief unfreeze sync device actions like read / write that are supported in "frozen state"
 */
status_rep_t sys_device_sync(uint8_t device_id);

/**
 * @brief freeze all devices
 */
status_rep_t sys_device_freeze_all(void);

/**
 * @brief sync all devices
 */
status_rep_t sys_device_sync_all(void);

sys_device_t* sys_device_get_by_id(uint8_t device_id);

/**
 * @brief Generic error handler for unhandled device errors.
 */
status_rep_t sys_device_generic_error_handler(sys_device_t* dev, status_rep_t* err);
