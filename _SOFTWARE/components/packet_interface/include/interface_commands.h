#pragma once

#include <stdint.h>
/*
INFO:
aviable commnads (general ones)
*/

typedef enum{
    PACKET_H_CONTEXT_CFG          = 0xF0,
    PACKET_H_INSTANCE             = 0xF1,
    PACKET_H_INSTANCE_SCALAR_DATA = 0xFA,
    PACKET_H_INSTANCE_ARR_DATA    = 0xFB,

    PACKET_H_LOOP_CFG             = 0xA0,
    PACKET_H_CODE_CFG             = 0xAA,

    PACKET_H_BLOCK_HEADER         = 0xB0,
    PACKET_H_BLOCK_INPUTS         = 0xB1,
    PACKET_H_BLOCK_OUTPUTS        = 0xB2,
    PACKET_H_BLOCK_DATA           = 0xBA,
    
    PACKET_H_SUBSCRIPTION_INIT    = 0xC0,
    PACKET_H_SUBSCRIPTION_ADD     = 0xC1,

    PACKET_H_PUBLISH              = 0xD0,
    
    PACKET_H_STATUS_LOG           = 0xE0,
    PACKET_H_ERROR_LOG            = 0xE1,

    PACKET_H_DEV_CFG              = 0x01,
    PACKET_H_DEV_RESET            = 0x02,
    PACKET_H_DEV_EMER_STOP        = 0x03,
    PACKET_H_EN_WIFI              = 0x04,
}packet_header_t;

typedef enum{
    DEV_TYPE_TPS = 0x01,
    DEV_TYPE_DRV = 0x02,
    DEV_TYPE_LM  = 0x03,
    DEV_TYPE_ADS = 0x04,
    DEV_TYPE_DAC = 0x05,
    DEV_TYPE_TCA = 0x06,
    DEV_TYPE_PCA = 0x07,
    DEV_TYPE_INA = 0x08,
    DEV_TYPE_AP  = 0x09,
}device_type_t;

static interface_dev_cfg_func interface_dev_cfg_setter_table[256] = {
    [DEV_TYPE_TPS]           = NULL, // To be implemented
    [DEV_TYPE_DRV]           = NULL, // To be implemented
    [DEV_TYPE_LM]            = NULL, // To be implemented
    [DEV_TYPE_ADS]           = NULL, // To be implemented
    [DEV_TYPE_DAC]           = NULL, // To be implemented
    [DEV_TYPE_TCA]           = NULL, // To be implemented
    [DEV_TYPE_PCA]           = NULL, // To be implemented
    [DEV_TYPE_INA]           = NULL, // To be implemented
    [DEV_TYPE_AP]            = NULL, // To be implemented
};

static interface_parse_func parse_dispatch_table[256] = {
    [PACKET_H_DEV_CFG]           = interface_parse_cmd_dev_cfg,
    [PACKET_H_DEV_RESET]         = NULL, // To be implemented
    [PACKET_H_DEV_EMER_STOP]     = NULL, // To be implemented
    [PACKET_H_EN_WIFI]           = NULL, // To be implemented
};
