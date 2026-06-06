#include "vm_restore.h"
#include "vm_code.h"
#include "rik_shared.h"
#include "manager_io.h"
#include "manager_power.h"
#include "rik_system_ctrl.h"
#include "rtos_utils.h"

status_rep_t vm_restore_io(void){
    R_MUTEX_LOCK(vm_mutex_code_running, WAIT_FOREVER);
    R_MUTEX_UNLOCK(vm_mutex_code_running);
    return STA_OK;
}
