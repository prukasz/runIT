#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "status.h"

#define MAX_DEVICE_ID 127
#define STATUS_PAYLOAD_DEVICE 1

#define SYS_DEV_MAKE_ERR_INFO(dev_id, err) (((uint64_t)(dev_id) << 32) | ((uint64_t)(err) & 0xFFFFFFFF))  // driver id and error in one payload

#define SYS_DEV_CHECK_HANDLE_R(handle) CHECK_HANDLE_X(handle, 1, 0, 0, STATUS_PAYLOAD_DEVICE)
#define SYS_DEV_CHECK_HANDLE_RP(handle) CHECK_HANDLE_X(handle, 1, 1, 0, STATUS_PAYLOAD_DEVICE)

#define SYS_DEV_STA_W(code, owner, info) STA_W(code, owner, info, STATUS_PAYLOAD_DEVICE)
#define SYS_DEV_STA_C(code, owner, info) STA_C(code, owner, info, STATUS_PAYLOAD_DEVICE)
#define SYS_DEV_STA_I(code, owner, info) STA_I(code, owner, info, STATUS_PAYLOAD_DEVICE)

#define SYS_DEV_CHECK_NOT_NULL_R(ptr) CHECK_NOT_NULL_X(ptr, 1, 0, 0, STATUS_PAYLOAD_DEVICE)
#define SYS_DEV_CHECK_NOT_NULL_RP(ptr) CHECK_NOT_NULL_X(ptr, 1, 1, 0, STATUS_PAYLOAD_DEVICE)

#define SYS_DEV_CHECK_DRIVER_CALL(driver_call, ctx)                                                                                                 \
  do {                                                                                                                                              \
    esp_err_t _err = (driver_call);                                                                                                                 \
    if (_err != ESP_OK) {                                                                                                                           \
      ESP_LOGE(__FILE_NAME__, "%s: ESP API Failed '%s' -> %s, device id %d", __func__, #driver_call, esp_err_to_name(_err), (ctx)->base.device_id); \
      return SYS_DEV_STA_C(ERR_ESP, OWNER, SYS_DEV_MAKE_ERR_INFO((ctx)->base.device_id, _err));                                                         \
    }                                                                                                                                               \
  } while (0)

typedef enum sys_device_role_t { SYS_DEV_ROLE_DEF, SYS_DEV_ROLE_IO, SYS_DEV_ROLE_PWR, SYS_DEV_ROLE_USER } sys_device_role_e;

typedef struct sys_device_t {
  /**
   * @brief Assigned id for identifying device
   */
  uint8_t device_id;
  /**
   * @brief Role of device, can be helpfull in error handling
   */
  sys_device_role_e role;
  /**
   * @brief provider layer handle or driver layer handle
   * @note self assigned as result of instal_device
   */
  void* device_handle;
  /**
   * @brief custom name for logging
   */
  const char* name;
  /**
   * @brief args for initialization of provider layer via provided function call
   * @note function from provider is required to self cast args
   */
  void** install_args;

  // Abstracted VTable
  /**
   * @brief Install device (aka create new)
   * @note (device manager -> provodier -> driver + sys io / power / etc)
   */
  void* (*install_device)(void** install_args);
  /**
   * @brief remove driver handle, all must be handled via provider
   */
  status_rep_t (*uninstall_device)(void* device_handle);
  /**
   * @brief reset device to predefined state in provider
   */
  status_rep_t (*reset_device)(void* device__handle);
  /**
   * @brief Take device only action in case of specific error
   * @note provider -> error struct -> error_handler -> overall action +
   * specific ations (functions)
   */
  status_rep_t (*error_handler)(void* device_handle, status_rep_t* error);

  /**
   * @brief suspend and save current state
   */
  status_rep_t (*suspend_device)(void* device_handle);

  /**
   * @brief resume from saved state
   */
  status_rep_t (*resume_device)(void* device_handle);

  /**
   * @brief freeze device updates and save actions in internal driver state
   * @note some operations may not suppport this mode and omit it
   */
  status_rep_t (*freeze_device)(void* device_handle);

  /**
   * @brief unfreeze sync device actions like read / write that are suppported in "frozen state"
   * @note this is optional double buffering
   */
  status_rep_t (*sync_device)(void* device_handle);

  /**
   * @brief Array of pointers to registered contract dispatch tables
   */
  void* contracts[5];  // 0: IO, 1: POWER_VREG, 2: POWER_MONITOR, 3: POWER_USB_PD, 4: H_BRIDGE

} sys_device_t;

typedef enum { SYS_DEVICE_CONTRACT_IO = 0, SYS_DEVICE_CONTRACT_POWER_VREG = 1, SYS_DEVICE_CONTRACT_POWER_MONITOR = 2, SYS_DEVICE_CONTRACT_POWER_USB_PD = 3, SYS_DEVICE_CONTRACT_H_BRIDGE = 4, SYS_DEVICE_CONTRACT_MAX = 5 } sys_device_contract_type_e;

#define SYS_DEV_GET_CONTRACT(dev, type) ((dev) ? (dev)->contracts[(type)] : NULL)

#define IF_SYS_DEV_AND_FEATURE(device_id, contract_type, contract_struct_type, func_member, dev_ptr, vtable_ptr)                                               \
  for (sys_device_t* dev_ptr = sys_device_get_by_id(device_id); dev_ptr; dev_ptr = NULL)                                                                       \
    for (const contract_struct_type* vtable_ptr = (const contract_struct_type*)SYS_DEV_GET_CONTRACT(dev_ptr, contract_type); vtable_ptr; vtable_ptr = NULL) \
      if (vtable_ptr->func_member)

#define SYS_DEV_DISPATCH(dev_id, contract_enum, contract_type, func_name, ...)                                                                                        \
  do {                                                                                                                                                                \
    IF_SYS_DEV_AND_FEATURE(dev_id, contract_enum, contract_type, func_name, dev_ptr, vtable_ptr) { return vtable_ptr->func_name(dev_ptr->device_handle, ##__VA_ARGS__); } \
    STA_RP(SYS_DEV_STA_W(ERR_NOT_SUPPORTED, OWNER, SYS_DEV_MAKE_ERR_INFO(dev_id, 0)));                                                                                    \
  } while (0)

#define SYS_DEV_ARG_PACK(val) ((void*)(uintptr_t)(val))
#define SYS_DEV_ARG_UNPACK(type, var, args, index) type var = (type)(uintptr_t)(args)[index]
#define SYS_DEV_ARG_UNPACK_VAL(type, args, index) ((type)(uintptr_t)(args)[index])

#define SYS_DEV_GET_ADAPTER_CONTEXT(ctx_type, hw_type, ctx_var, hw_var, input_handle) \
  ctx_type* ctx_var = (ctx_type*)(input_handle);                                      \
  SYS_DEV_CHECK_HANDLE_R(ctx_var);                                                        \
  hw_type hw_var = (hw_type)((ctx_var)->base.hw_handle);                              \
  SYS_DEV_CHECK_HANDLE_R(hw_var)

#define IF_SYS_DEV_FROZEN(ctx) if ((ctx)->base.is_frozen)
#define SYS_DEV_CTX_FREEZE(ctx) (ctx)->base.is_frozen = true
#define SYS_DEV_CTX_UNFREEZE(ctx) (ctx)->base.is_frozen = false

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

/**
 * get device struct by providing id
 */
sys_device_t* sys_device_get_by_id(uint8_t device_id);
