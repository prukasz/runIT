#include "status_codes.h" // Include your header
#include "stdint.h"
#include "status.h"


const char *status_owner_to_name(uint32_t owner) {
    switch ((status_owner_e)owner) {
        
#define X_ENTRY(name, value, str_name) case name: return str_name;
        GLOBAL_OWNER_MAP(X_ENTRY)
#undef X_ENTRY

        default: 
            return "UNKNOWN_OWNER";
    }
}

const char *status_error_to_name(uint32_t error_code) {
    switch ((status_code_e)error_code) {
        
#define X_ENTRY(name, value, str_name) case name: return str_name;
        GLOBAL_ERROR_MAP(X_ENTRY)
#undef X_ENTRY

        default: 
            return "UNKNOWN_ERROR";
    }
}
