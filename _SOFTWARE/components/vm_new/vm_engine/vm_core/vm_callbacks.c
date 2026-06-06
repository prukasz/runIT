#include "vm_callbacks.h"
#include "rik_system_ctrl.h"

static vm_callbacks_list_t *_cb_list_get(vm_callback_types_t callback_type)
{
    if (current_code == NULL) return NULL;
    if (callback_type >= 6) return NULL;
    return &current_code->callback_lists[callback_type];
}

static void _cb_list_add(vm_code_section_callback_t **cb_list, size_t *current_count, vm_code_section_callback_t cb)
{
    if (cb_list == NULL || current_count == NULL) return; 
    size_t new_size = (*current_count + 1) * sizeof(vm_code_section_callback_t);

    if (*cb_list == NULL) {
        *cb_list = malloc(new_size);
    } else {
        *cb_list = realloc(*cb_list, new_size);
    }

    if (*cb_list != NULL) {
        (*cb_list)[*current_count] = cb;
        (*current_count)++;
    }
}

#undef OWNER
#define OWNER VM_OWNER_CALLBACK_ADD
status_rep_t vm_callback_section_add(vm_code_section_t *section, uint32_t event_type_match, vm_callback_types_t callback_type){
    vm_callbacks_list_t *cb_list = _cb_list_get(callback_type);
    if(cb_list == NULL){STA_RP(STA_C(VM_ERR_CB_TYPE_INVALID, OWNER, callback_type));}

    vm_code_section_callback_t new_section = {.section = section, .event_type_match = event_type_match};
    _cb_list_add((vm_code_section_callback_t **)&cb_list->vm_callback_section_list, &cb_list->list_size, new_section);
    return STA_OK;
}



static void _cb_list_remove(vm_code_section_callback_t **cb_list, size_t *current_count, vm_code_section_t *section, uint32_t event_type_match)
{
    if (cb_list == NULL || current_count == NULL || *cb_list == NULL) return; 

    for (size_t i = 0; i < *current_count; i++) {
        if ((*cb_list)[i].section == section && (*cb_list)[i].event_type_match == event_type_match) {
            for (size_t j = i; j < *current_count - 1; j++) {
                (*cb_list)[j] = (*cb_list)[j + 1];
            }
            (*current_count)--;
            if (*current_count == 0) {
                free(*cb_list);
                *cb_list = NULL;
            } else {
                *cb_list = realloc(*cb_list, (*current_count) * sizeof(vm_code_section_callback_t));
            }
            break;
        }
    }
}

#undef OWNER
#define OWNER VM_OWNER_CALLBACK_REMOVE
status_rep_t vm_callback_section_remove(vm_code_section_t *section, uint32_t event_type_match, vm_callback_types_t callback_type){
    vm_callbacks_list_t *cb_list = _cb_list_get(callback_type);
    if(cb_list == NULL){STA_RP(STA_C(VM_ERR_CB_TYPE_INVALID, OWNER, callback_type));}

    _cb_list_remove((vm_code_section_callback_t **)&cb_list->vm_callback_section_list, &cb_list->list_size, section, event_type_match); 
    return STA_OK;
}
#undef OWNER

void vm_callback_sys_gpio(void *arg){
    vm_callbacks_list_t gpio_cb_list = current_code->callback_lists[VM_SYS_CALLBACK_GPIO];
    uint32_t pin_num = (uint32_t)arg;
    for(size_t i = 0; i < gpio_cb_list.list_size; i++){
        vm_code_section_callback_t *cb = (vm_code_section_callback_t*)gpio_cb_list.vm_callback_section_list + i;
        if(cb->event_type_match == pin_num){
            cb->section->pending_execution = 1;
        }
    }
}

void vm_callback_sys_adc(void *arg){
    vm_callbacks_list_t adc_cb_list = current_code->callback_lists[VM_SYS_CALLBACK_ADC];
    uint32_t pin_num = (uint32_t)arg;
    for(size_t i = 0; i < adc_cb_list.list_size; i++){
        vm_code_section_callback_t *cb = (vm_code_section_callback_t*)adc_cb_list.vm_callback_section_list + i;
        if(cb->event_type_match == pin_num){
            cb->section->pending_execution = 1;
        }
    }
}

void vm_callback_sys_power(void *arg){
    vm_callbacks_list_t power_cb_list = current_code->callback_lists[VM_SYS_CALLBACK_POWER_EVENT];
    uint32_t pwr_event_type = (uint32_t)arg;
    for(size_t i = 0; i < power_cb_list.list_size; i++){
        vm_code_section_callback_t *cb = (vm_code_section_callback_t*)power_cb_list.vm_callback_section_list + i;
        if(cb->event_type_match == pwr_event_type){
            cb->section->pending_execution = 1;
        }
    }
}

/* Register VM's system callbacks with kernel so kernel doesn't need to reference
 * VM symbols directly. Use a constructor to set handlers at startup.
 */
__attribute__((constructor)) static void _vm_register_system_callbacks(void){
    rik_register_vm_sys_gpio(vm_callback_sys_gpio);
    rik_register_vm_sys_adc(vm_callback_sys_adc);
    rik_register_vm_sys_power(vm_callback_sys_power);
}

