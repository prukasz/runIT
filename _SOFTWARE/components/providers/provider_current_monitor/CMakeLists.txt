idf_component_register(
    SRCS "provider_current_monitor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ina3221 
    REQUIRES status
)
