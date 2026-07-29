#pragma once
#include <stdint.h>

#define SYS_INTERFACE_OWNER_MAP(X)                                    \
  X(OWNER_SYS_INTERFACE_BASE, 0xA600, "OWNER_SYS_INTERFACE_BASE")     \
  X(OWNER_SYS_INTERFACE_DECODE, 0xA601, "OWNER_SYS_INTERFACE_DECODE") \
  X(OWNER_SYS_INTERFACE_CLASS, 0xA602, "OWNER_SYS_INTERFACE_CLASS")   \
  X(OWNER_SYS_INTERFACE_SOURCE, 0xA603, "OWNER_SYS_INTERFACE_SOURCE") \
  X(OWNER_DEC_SYS_CONTRACTS, 0xA610, "OWNER_DEC_SYS_CONTRACTS")     \
  X(OWNER_DEC_SYS_ACTIONS, 0xA611, "OWNER_DEC_SYS_ACTIONS")         \
  X(OWNER_DEC_SYS_DEVICE_INSTALL, 0xA612, "OWNER_DEC_SYS_DEVICE_INSTALL") \
  X(OWNER_ENC_SYS_ERRORS, 0xA620, "OWNER_ENC_SYS_ERRORS")

#define SYS_ERROR_INTERFACE_MAP(X)                                                                        \
  X(ERR_INTERFACE_SHORT_FRAME, struct { uint32_t got; uint32_t need; })                                    \
  X(ERR_INTERFACE_UNKNOWN_CLASS, struct { uint8_t class_header; })                                         \
  X(ERR_INTERFACE_UNKNOWN_PACKET, struct { uint8_t class_header; uint8_t packet_header; })                 \
  X(ERR_INTERFACE_CLASS_TAKEN, struct { uint8_t class_header; })                                           \
  X(ERR_INTERFACE_NO_CLASS_SLOTS, struct { uint8_t class_header; })                                        \
  X(ERR_INTERFACE_ENC_BUF_TOO_SMALL, struct { uint32_t got; uint32_t need; })
