#include "vm_code.h"
#include "vm_callbacks.h"
#include "vm_runner.h"

#define TAG __FILE_NAME__
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

status_rep_t vm_code_runner(){
    ESP_LOGI(TAG, "Running vm...");
    if (vm_active_code == NULL) {
        return STA_OK;
    }
    vm_code_section_t *executable_section = NULL;
        
    /* Scan all sections to find highest priority with pending execution */
    for (uint16_t i = 0; i < vm_active_code->sections_cnt; i++) {
        vm_code_section_t *section = &vm_active_code->sections[i];
            
        /* Only consider sections with pending execution flag */
        if (!section->pending_execution) {
            continue;
        }
            
            /* Check if section counter has not exceeded starvation threshold */
        if (section->wtd_cnt >= STARVATION_THRESHOLD && !section->is_idle) {
            continue;
        }
            
            /* Track highest priority executable section */
        if (executable_section == NULL || section->priority > executable_section->priority) {
            executable_section = section;
        }
    }
        
        /* If found an executable section, execute it */
    if (executable_section != NULL) {
        status_rep_t exec_result = execute_node(executable_section);
        /* Anti-starvation: clear all section counters after execution */
        /* This allows lower-priority sections to run next iteration */
        for (uint16_t i = 0; i < vm_active_code->sections_cnt; i++) {
            vm_active_code->sections[i].wtd_cnt = 0;
        }
        if(!executable_section->is_idle){
        executable_section->pending_execution = false;
        }
    }else {
    }
    return STA_OK;
}
