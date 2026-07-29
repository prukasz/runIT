idf_component_register(
    SRCS "sys_actions.c"
    INCLUDE_DIRS "include"
                 # sys_actions.h references sys_state_e (a type only, no calls
                 # into sys_states) - a plain include dir avoids sys_states and
                 # sys_actions REQUIRES-ing each other (sys_states.c genuinely
                 # calls into sys_actions, so that direction owns the real
                 # REQUIRES; see sys_errors' CMakeLists for the same pattern).
                 "../sys_states/include"
    REQUIRES sys_errors
    PRIV_REQUIRES utils sys_interface codecs nvs_flash
)
