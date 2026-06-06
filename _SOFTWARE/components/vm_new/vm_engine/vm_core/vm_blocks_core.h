#pragma once
#include "vm_variables.h"

/**
 *  Block has assigned fucntion
 *  Each block can read N inpust, each input is represented by accessor, pointing to variable
 *  Each block can have M outputs, each output is represented by accessor, pointing to variable assigned to this block
 */


/** 
 * @brief Unified block structure 
 */
typedef struct{
    vm_var_accessor_t **inputs;   /*list of inputs and outputs as pointers to accessors*/
    vm_var_accessor_t **outputs;  
    void *custom_data; 
    struct {
        uint16_t block_idx; 
        uint16_t node_idx;          
        uint16_t current_connected_in; 
        uint8_t  block_type;           
        uint8_t  in_cnt;               
        uint8_t  q_cnt;                
        uint8_t  status;
    }cfg;
}vm_block_data_t;



