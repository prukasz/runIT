#pragma once
#include "interface_dispatcher.h"

typedef enum{
    PACKET_H_CFG_PWR = 0x01,
    PACKET_H_CFG_IO = 0x02,
    PACKET_H_CFG_SYS = 0x03,
    PACKET_H_CFG_TESTS = 0x04
}packet_header_t;



