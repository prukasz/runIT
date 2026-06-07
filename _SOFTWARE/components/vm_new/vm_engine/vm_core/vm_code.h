#pragma once 
#include <stdint.h>
#include "vm_blocks_core.h"
#include "rtos_utils.h"
#include "vm_mem_management.h"

/**
 * Node is single execution point in code, it can be simple block or lot of self dependent blocks.
 * Code is made of nodes 
 * section is collections of nodes that are required to be executed together 
 * 
 * ---0----0---0   ]            ]           ]
 * 0--^            ]<--Node     ]<--Section ]
 *                                          ]   
 *                                          ]
  * ---0----0---0   ]           ]           ] <--CODE
 * 0--^             ]<--Node    ]           ]
 *                              ]<--Section ] 
 * ---0----0-      ]            ]           ]
 * 0--^            ]<--Node     ]           ]
 */

typedef struct{ 
    vm_block_data_t * blocks_list;  /* list of block objects */
    uint16_t blocks_count;           /* count of blocks in list*/
    struct{
        uint16_t flags;
    }cfg;
}vm_code_node_t;

typedef struct{ 
    uint16_t nodes_cnt;             /* count of nodes in section*/
    uint8_t priority;              
    volatile bool pending_execution;    
    bool is_idle;   /*is section pending for execution flag*/
    /* when counter over predefined val execution is given to lower priority section
    to prevent only one loop runnning, reseted after other section has been executed, increased after execution*/
    volatile uint8_t wtd_cnt;  
    vm_code_node_t *nodes;         /* list of nodes*/
}vm_code_section_t; 

typedef struct{
    vm_code_section_t *section; /* section to execute when callback is triggered */
    uint32_t event_type_match; /* type of event to trigger callback, for example power event type or pin num */
}vm_code_section_callback_t;

typedef struct{
    size_t list_size;
    vm_code_section_callback_t* vm_callback_section_list;
}vm_callbacks_list_t;

typedef struct {
    uint16_t blocks_cnt;            /* Total blocks across all sections */
    uint16_t nodes_cnt;             /* Total nodes across all sections */
    uint16_t sections_cnt;          /* Total sections */
    vm_code_section_t *sections;    /* Flat array of sections */
    vm_block_data_t   *blocks_storage;   /*storage for all blocks*/
    vm_code_node_t    *nodes_storage;         /* The actual flat storage of all nodes */
    vm_code_section_t *sections_storage;    /* storage for all sections */
    vm_callbacks_list_t callback_lists[6];    
    vm_mem_context_t vm_mem_t[CONTEXTS_CNT];     
} vm_code_t;

static vm_code_t *vm_active_code; 

void vm_code_set_active(vm_code_t *code){
    vm_active_code = code;
}

void vm_code_clear_active(){
    vm_active_code = NULL;
}





