### Brief: Prefered way to handle error, please use macros for semantic clarity, if there is no specific macro look for it also in related sys component header.
### Lookups
* typedef struct { \
  uint32_t e_code; \
  uint32_t e_owner; \
  uint64_t payload; \
  struct { \
    uint8_t severity : 2; \
    uint8_t payload_type : 6; \
  } details; \
} status_rep_t; 
* #define STATUS_INFO 0
* #define STATUS_WARNING 1
* #define STATUS_CRITICAL 2
* void status_assign_error_tx(uint16_t char_uuid, uint8_t buffer_id, QueueHandle_t status_queue);
* void status_set_rep_mode(uint8_t rep_level);
* void status_resume();
* void status_suspend();
* STA_OK
* STA_C(e_code, e_owner, payload, payload_type) 
* STA_W(e_code, e_owner, payload, payload_type) 
* STA_I(e_code, e_owner, payload, payload_type) 
* STA_R_ON_ERR(status)
* STA_P_ON_ERR(status)
* STA_RP_ON_ERR(status)
* STA_P(status) 
* STA_RP(status)
* CHECK_HANDLE_R(handle)
* CHECK_HANDLE_P(handle)
* CHECK_HANDLE_RP(handle)
* #define CHECK_ESP_CALL_R(esp_err_call)
* #define CHECK_ESP_CALL_P(esp_err_call)
* #define CHECK_ESP_CALL_RP(esp_err_call)
* CHECK_ARG_R(arg, min_val, max_val, override_return)
* CHECK_ARG_RP(arg, min_val, max_val, override_return)
* CHECK_NOT_NULL_R(ptr)
* CHECK_NOT_NULL_RP(ptr)

### Terminology: status_rep_t -> main error structure:
* OWNER -> Where error occured function name or file scope, prefix 
OWNER_xxx
* #define OWNER OWNER_xxx -> macros expect this define before use 
* ERROR_CODE -> What error occured, prefix ERROR_xxx 
* PAYLOAD -> extra debugging info 8 bytes
* SEVERITY -> how error is treated CRITICAL WARNING INFO
* PAYLOAD_TYPE -> what PAYLOAD CONTAINS
### MACROS NAMING:
* STA -> uses on status_rep_t structure
* R -> return (on condition or always)
* P -> Push error to error stream
* C -> Critical 
* W -> Warning
* I -> Info
### ERORR PUSHING:
* At lifecycle end - error shall be pushed (driver -> device -> sys (sys make push) )
* Pushed error structure goes into assigned queue and can be transmited to remote host
* status_assign_error_tx(char_id, buffer_id, queue) -> buffers config
* status_set_rep_mode(rep_level) -> above or equal rep_level will be added to queue 
* status_suspend() -> ignore adding example: when handling error can cause next
* status_resume() -> restores last used rep_level
### READY TO USE MACROS:
* STA_OK -> return STA_OK; => no error
* STA_C, STA_W, STA_I (ERROR_CODE, OWNER, PAYLOAD, PAYLOAD_TYPE) -> simplification of syntax
* STA_RP(stauts) and STA_P(status) -> return and push or only push status 
* STA_IS_ERR(status) and STA_IS_OK(status) -> bool yes or no
* STA_R/RP/P_ON_ERR(status) -> check if there is error and do action
* STA_FROM_ESP(esp_err_t) -> STA_C(ERR_ESP, OWNER, esp_err_t,  STATUS_PAYLOAD_ESP_ERR) if erorr occur, else STA_OK
### EXTRA MACROS
* CHECK_HANDLE_X -> if handle is NULL then print handle name and R/P/RP STA_C(ERR_MISSING_HANDLE, OWNER, payload, payload_type) -> you can create module specific handle checking 
* CHECK_HANDLE_R/P/RP -> default one returns ERR_MISSING_HANDLE and no payload
* CHECK_NOT_NULL_X -> similar to check handle STA_C(ERR_INVALID_ARG, OWNER, payload, payload_type)
* CHECK_ARG_X -> check if value in bounds, else logs condition, override_return: if not "0" then pasted as payload
* CHECK_ESP_CALL_X(esp_err_t) -> STA_FROM_ESP but logs error and R/P/RP status on error

### ERROR CODES ADDING:
* GLOBAL_ERROR_MAP(X) -> Add new error block
* GLOBAL_OWNER_MAP(X) -> Add new owner block
* example:

#define X_OWNER_MAP(X)\
  X(OWNER_xxx_BASE, 0xD000, "OWNER_xxx_BASE") \
  X(OWENR_xxx, 0xD001, "OWENR_xxx or description") \
then add it to GLOBAL_OWNER_MAP(X)
X_OWNER_MAP(X)

### Directory structure: 
components/status/codes/sys -> system OWNERS AND CODES
components/status/codes/status_codes.h -> master include file linking all other files
components/status/status.h -> main include file #include "status.h"
components/status/status.c -> config functions etc


