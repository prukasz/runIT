#pragma once 
#include <stdint.h>
#include "vm_blocks_core.h"

typedef struct{ 
    vm_block_data_t ** blocks_list;
    uint16_t blocks_count; 
    struct{
        uint16_t flags;
    }cfg;
}vm_code_node_t;

typedef struct{
    uint16_t nodes_cnt;
    uint16_t _reserved;
    vm_code_node_t nodes[];
}vm_code_t;

typedef struct{ 
    uint16_t nodes_cnt;
    uint16_t _reserved;
    vm_code_node_t **nodes;
}vm_code_branch_t;


