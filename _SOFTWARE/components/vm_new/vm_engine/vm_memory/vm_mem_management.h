#pragma once
#include <stdint.h>
#include "vm_variables.h"
#define TYPES_CNT 8
#define CONTEXTS_CNT 8

typedef struct{
    uint8_t *_pool_start;
    uint8_t *next_free;
    uint32_t size_cap;
}vm_linear_allocator_t;

typedef struct{
    uint32_t size_cap;
    uint32_t current_size;
    uint8_t data[];
}vm_buff_t;



typedef struct{
    vm_linear_allocator_t var_data[TYPES_CNT];

    vm_linear_allocator_t var_instances[TYPES_CNT];
    vm_var_instance_t **instances;

    vm_linear_allocator_t  var_accessors[TYPES_CNT];
    vm_var_accessor_t **accessors;
}vm_mem_context_t;

extern vm_mem_context_t vm_mem_t[CONTEXTS_CNT];






