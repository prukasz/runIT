#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_error_vm.h"
#include "vm_alloc.h"
#include "vm_block.h"
#include "vm_obj.h"
#include "vm_obj_access.h"

typedef struct vm_block_data_t vm_block_data_t;

typedef struct {
  vm_block_data_t** blocks_list;  // self-dependent chain of linked blocks; blocks
                                  // are variable-size (vm_block.h), so this must be
                                  // an array of pointers, not a directly-indexable array
  uint16_t blocks_count;          /* count of blocks in list*/
  struct {
    uint16_t flags;
  } cfg;
} vm_code_node_t;

typedef struct {
  uint16_t nodes_cnt; /* count of nodes in section*/
  uint8_t priority;
  volatile bool pending_execution;
  bool is_idle; /*is section pending for execution flag*/
  /* when counter over predefined val execution is given to lower priority section
  to prevent only one loop runnning, reseted after other section has been executed, increased after execution*/
  volatile uint8_t wtd_cnt;
  vm_code_node_t* nodes; /* list of nodes*/
} vm_code_section_t;

typedef struct {
  struct {
    vm_alloc_t arena;
  } blocks;
  struct {
    uint16_t objects_cnt;
    vm_obj_t** objects;
    vm_alloc_t arena;
  } objects;
  struct {
    uint16_t accessors_cnt;
    vm_accessor_t** accessors;
    vm_alloc_t arena;
  } accessors;
  struct {
    uint16_t nodes_cnt;
    vm_code_node_t** nodes;
    vm_alloc_t arena;
  } nodes;
  struct {
    uint16_t sections_cnt;
    vm_code_section_t** sections;
    vm_alloc_t arena;
  } sections;
} vm_code_t;
