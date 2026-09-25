#pragma once
#include <stdint.h>
#include <stdio.h>

#define SYS_INTERFACE_OWNER_MAP(X)                                    \
  X(OWNER_SYS_INTERFACE_BASE, 0xA600, "OWNER_SYS_INTERFACE_BASE")     \
  X(OWNER_SYS_INTERFACE_DECODE, 0xA601, "OWNER_SYS_INTERFACE_DECODE") \
  X(OWNER_SYS_INTERFACE_CLASS, 0xA602, "OWNER_SYS_INTERFACE_CLASS")   \
  X(OWNER_SYS_INTERFACE_SOURCE, 0xA603, "OWNER_SYS_INTERFACE_SOURCE") \
  X(OWNER_SYS_INTERFACE_RESPOND, 0xA604, "OWNER_SYS_INTERFACE_RESPOND") \
  X(OWNER_DEC_SYS_CONTRACTS, 0xA610, "OWNER_DEC_SYS_CONTRACTS")     \
  X(OWNER_DEC_SYS_ACTIONS, 0xA611, "OWNER_DEC_SYS_ACTIONS")         \
  X(OWNER_DEC_SYS_DEVICE_INSTALL, 0xA612, "OWNER_DEC_SYS_DEVICE_INSTALL") \
  X(OWNER_DEC_SETTINGS_BLE, 0xA613, "OWNER_DEC_SETTINGS_BLE")      \
  X(OWNER_DEC_SETTINGS_DATA_CONNECTOR, 0xA614, "OWNER_DEC_SETTINGS_DATA_CONNECTOR") \
  X(OWNER_DEC_SETTINGS_LOGS, 0xA615, "OWNER_DEC_SETTINGS_LOGS")    \
  X(OWNER_DEC_FEATURES, 0xA616, "OWNER_DEC_FEATURES")              \
  X(OWNER_DEC_SETTINGS_POWER, 0xA617, "OWNER_DEC_SETTINGS_POWER")  \
  X(OWNER_DEC_SYS_EVENTS, 0xA618, "OWNER_DEC_SYS_EVENTS")          \
  X(OWNER_ENC_SYS_ERRORS, 0xA620, "OWNER_ENC_SYS_ERRORS")

#define SYS_ERROR_INTERFACE_MAP(X)                                                                                                      \
  X(ERR_INTERFACE_SHORT_FRAME, 0xA601, SE_LEVEL_LOW, struct { uint32_t got; uint32_t need; })                                    \
  X(ERR_INTERFACE_UNKNOWN_CLASS, 0xA602, SE_LEVEL_LOW, struct { uint8_t class_header; /*@id rx-class*/ })                                         \
  X(ERR_INTERFACE_UNKNOWN_PACKET, 0xA603, SE_LEVEL_LOW, struct { uint8_t class_header; /*@id rx-class*/ uint8_t packet_header; /*@id rx-packet class_header*/ })                 \
  X(ERR_INTERFACE_CLASS_TAKEN, 0xA604, SE_LEVEL_LOW, struct { uint8_t class_header; /*@id rx-class*/ })                                           \
  X(ERR_INTERFACE_NO_CLASS_SLOTS, 0xA605, SE_LEVEL_MEDIUM, struct { uint8_t class_header; /*@id rx-class*/ })                                        \
  X(ERR_INTERFACE_ENC_BUF_TOO_SMALL, 0xA606, SE_LEVEL_HIGH, struct { uint32_t got; uint32_t need; })                          \
  X(ERR_INTERFACE_RESPONSE_TOO_LONG, 0xA607, SE_LEVEL_MEDIUM, struct { uint32_t got; uint32_t max; })

/** @brief Human-readable descriptions for the sys_interface tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_INTERFACE_LOGGER_MAP(X) \
  X(ERR_INTERFACE_SHORT_FRAME)            \
  X(ERR_INTERFACE_UNKNOWN_CLASS)          \
  X(ERR_INTERFACE_UNKNOWN_PACKET)         \
  X(ERR_INTERFACE_CLASS_TAKEN)            \
  X(ERR_INTERFACE_NO_CLASS_SLOTS)         \
  X(ERR_INTERFACE_ENC_BUF_TOO_SMALL)       \
  X(ERR_INTERFACE_RESPONSE_TOO_LONG)

#define LOG_BODY_ERR_INTERFACE_SHORT_FRAME(p, out, out_size) snprintf((out), (out_size), "frame too short: got %lu bytes, need %lu", (unsigned long)(p)->got, (unsigned long)(p)->need)
#define LOG_BODY_ERR_INTERFACE_UNKNOWN_CLASS(p, out, out_size) snprintf((out), (out_size), "unregistered class byte 0x%02X", (p)->class_header)
#define LOG_BODY_ERR_INTERFACE_UNKNOWN_PACKET(p, out, out_size) \
  snprintf((out), (out_size), "unmapped packet 0x%02X in class 0x%02X", (p)->packet_header, (p)->class_header)
#define LOG_BODY_ERR_INTERFACE_CLASS_TAKEN(p, out, out_size) snprintf((out), (out_size), "class byte 0x%02X is already registered", (p)->class_header)
#define LOG_BODY_ERR_INTERFACE_NO_CLASS_SLOTS(p, out, out_size) \
  snprintf((out), (out_size), "no free class slots left (registering 0x%02X)", (p)->class_header)
#define LOG_BODY_ERR_INTERFACE_ENC_BUF_TOO_SMALL(p, out, out_size) \
  snprintf((out), (out_size), "encode buffer too small: got %lu bytes, need %lu", (unsigned long)(p)->got, (unsigned long)(p)->need)
#define LOG_BODY_ERR_INTERFACE_RESPONSE_TOO_LONG(p, out, out_size) \
  snprintf((out), (out_size), "response data too long: %lu bytes, max %lu", (unsigned long)(p)->got, (unsigned long)(p)->max)
