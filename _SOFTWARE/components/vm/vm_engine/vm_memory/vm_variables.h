#pragma once
#include <stdint.h>
#include <stdbool.h>



typedef enum {
    VAR_U8     = 0,
    VAR_U16    = 1,
    VAR_U32    = 2,
    VAR_I16    = 3,
    VAR_I32    = 4,
    VAR_B      = 5,
    VAR_F      = 6,
    VAR_STRING = 7  /* Added to properly route var_string_t payloads */
} vm_var_types_t;

static const uint8_t VM_VAR_TYPE_SIZES[VAR_TYPES_COUNT] = {
    1, // U8
    2, // U16
    4, // U32
    2, // I16
    4, // I32
    1, // B
    4, // F
    1  // STRING (Size of 1 char, handled dynamically)
};

/* Forward declaration for accessors */
typedef struct vm_var_accessor_s vm_var_accessor_t;


typedef struct {
    uint16_t max_len; /*maximum buffer lenght*/   
    uint16_t current_len; /*current text lenght*/
    char data[];  /*actuall text*/
} vm_var_string_t;


typedef struct {
    uint16_t el_cnt;   /*count of elements (type defined in instance)*/
    uint16_t dims_cnt; /*N dimensional*/

    /*
    * N*uint32_t dimension size 
    * el_cnt * element (type defined in instance)
    */
    uint8_t data[]; 
} vm_var_array_t;


typedef union {
    uint8_t      *u8;
    uint16_t     *u16;
    uint32_t     *u32;
    int16_t      *i16;
    int32_t      *i32;
    float        *f;
    bool         *b;
    vm_var_array_t  *array;  /* Standard VM array payload */
    vm_var_string_t *string; /* Standard VM string payload */
    void         *raw;    /* For is_standalone == 1 (Hardware memory mapping) */
} vm_var_type_ptr_u;

typedef union {
    uint8_t  u8;
    uint16_t u16;
    uint32_t u32;
    int16_t  i16;
    int32_t  i32;
    float    f;
    bool     b;
} vm_var_type_val_u;


typedef union {
    vm_var_type_ptr_u ptr;
    vm_var_type_val_u val;
} vm_var_storage_t;

typedef struct {
    vm_var_storage_t data;  /*Value or pointer to it*/
    uint8_t type;        /*type of variable*/
    uint8_t context;     /*In what context located*/
    
    struct {
        uint16_t upd            : 1; /*was updated in cylcle flag*/
        uint16_t upd_resetable  : 1; /*can vm reset flag*/
        uint16_t constant       : 1; /*is this constant*/
        uint16_t is_standalone  : 1; /*is ptr or not*/
        uint16_t is_usr_mutable : 1; /*can user edit*/
        uint16_t is_subscribed  : 1; /*for debug*/
        uint16_t retentive      : 1; /*future nvs*/
        uint16_t _reserved      : 9;
    }flags; 
} vm_var_instance_t;


/**
 * @brief Indexing type for table
*/
typedef union {
    vm_var_accessor_t *by_reference; /*Index is other accessor*/
    uint32_t        value;        /*Index is static value*/
} vm_var_index_type;


/**
 * @brief variable_instance_t -> array type accessor
 */
typedef struct {
    uint8_t indices_cnt;
    uint8_t indices_types_mask; /* Bitmask: 0 = static value, 1 = dynamic by_reference */
    uint8_t is_resolved;        
    uint8_t accessed_whole;        
    
    uint32_t cached_flat_index; /* Stored resolution of N-dimensional math */
    
    vm_var_index_type indices[];   /* Flexible array of indices */
} vm_var_accessor_array_t;

/**
 * @brief variable_instance_t -> array type accessor
 */
typedef union {
    vm_var_accessor_array_t* array; /*chosen array element*/
    uint32_t              string_char_idx; /*chosen string char*/
} vm_var_accessor_specific_t;


struct vm_var_accessor_s {
    uint16_t var_index;               /* 2 bytes: Index within the type instances */
    uint8_t  context;                 /* 1 byte: Which context owns the variable */
    uint8_t  type;                    /* 1 byte: Expected mem_types_t */
    vm_var_accessor_specific_t details;  /* 4 bytes: Specifiers */
};
