#include "vm_code.h"
#include "vm_callbacks.h"
#include "rik_system_ctrl.h"

/* Starvation prevention threshold: skip high-priority sections after N attempts */
#define STARVATION_THRESHOLD 5

status_rep_t execute_node(vm_code_section_t *section){
    //execute all blocks in node, handle errors, return status
    return STA_OK;
}

/**
 * Priority-based scheduler with starvation prevention
 * 2. Find highest priority section with pending_execution flag set
 * 3. If section counter < threshold, execute it
 * 4. After execution, clear all counters (anti-starvation reset)
 * 5. Keep all execution flags intact
 * 6. If section counter >= threshold, skip to lower priority section
 */

volatile bool vm_can_run = false;

status_rep_t vm_code_runner(){
    if (current_code == NULL) {
        return STA_OK;
    }
    
    while(vm_can_run){
        vm_code_section_t *executable_section = NULL;
        
        /* Scan all sections to find highest priority with pending execution */
        for (uint16_t i = 0; i < current_code->sections_cnt; i++) {
            vm_code_section_t *section = &current_code->sections[i];
            
            /* Only consider sections with pending execution flag */
            if (!section->pending_execution) {
                continue;
            }
            
            /* Check if section counter has not exceeded starvation threshold */
            if (section->wtd_cnt >= STARVATION_THRESHOLD) {
                continue;
            }
            
            /* Track highest priority executable section */
            if (executable_section == NULL || section->priority > executable_section->priority) {
                executable_section = section;
            }
        }
        
        /* If found an executable section, execute it */
        if (executable_section != NULL) {
            R_MUTEX_LOCK(vm_mutex_code_running, WAIT_FOREVER);
            sys_freeze();
            status_rep_t exec_result = execute_node(executable_section);
            /* Anti-starvation: clear all section counters after execution */
            /* This allows lower-priority sections to run next iteration */
            for (uint16_t i = 0; i < current_code->sections_cnt; i++) {
                current_code->sections[i].wtd_cnt = 0;
            }
            R_MUTEX_UNLOCK(vm_mutex_code_running);
            sys_unfreeze();
            
            /* Execution flags remain intact for next iteration */
        } else {
            /* No executable section found at this moment - no action needed */
            /* Next iteration will check again as flags remain set */
        }
    }
    
    return STA_OK;
}
