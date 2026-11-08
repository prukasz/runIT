#pragma once

/*
The serial protocol of the ESP32-S3's ROM bootloader (SYS_FLASHER.MD "The ROM's protocol"), as pure functions: SLIP
framing, a command's packet, its checksum, an answer's parse. No platform: sys_flasher.c sends and reads, the host
tests check these. Not on the component's public include path.
*/

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_FLASHER_SLIP_END     0xC0u  // starts and ends a packet
#define SYS_FLASHER_SLIP_ESC     0xDBu
#define SYS_FLASHER_SLIP_ESC_END 0xDCu  // 0xC0 in a packet: DB DC
#define SYS_FLASHER_SLIP_ESC_ESC 0xDDu  // 0xDB in a packet: DB DD

#define SYS_FLASHER_ROM_HEAD     8u     // [direction][op][size u16][checksum or value u32]
#define SYS_FLASHER_ROM_STATUS   4u     // an answer's data ends [status][error][0][0] (the ESP32-S3 ROM's)
#define SYS_FLASHER_ROM_BLOCK    0x400u // FLASH_DATA's data, the ROM's block
#define SYS_FLASHER_ROM_SEED     0xEFu  // the checksum's seed

/* The ROM's ops. */
#define SYS_FLASHER_ROM_FLASH_BEGIN   0x02u
#define SYS_FLASHER_ROM_FLASH_DATA    0x03u
#define SYS_FLASHER_ROM_SYNC          0x08u
#define SYS_FLASHER_ROM_SPI_PARAMS    0x0Bu
#define SYS_FLASHER_ROM_SPI_ATTACH    0x0Du
#define SYS_FLASHER_ROM_CHANGE_BAUD   0x0Fu
#define SYS_FLASHER_ROM_SPI_FLASH_MD5 0x13u

/** @brief The longest SLIP packet of `len` bytes: every byte escaped, an END each side. */
#define SYS_FLASHER_SLIP_MAX(len) (2u * (len) + 2u)

/** @brief An answer of the ROM, pointing into the decoded packet. */
typedef struct {
  uint8_t op;           /**< The op it answers */
  uint32_t value;       /**< The head's value field (a register read's value; else 0) */
  const uint8_t* data;  /**< Its data, the status bytes not included */
  uint32_t len;         /**< Bytes in `data` */
  uint8_t status;       /**< 0: done */
  uint8_t error;        /**< The ROM's error code when `status` is not 0 */
} sys_flasher_rom_answer_t;

/** @brief A SLIP decoder's state: the packet so far, in the caller's buffer. */
typedef struct {
  uint8_t* buf;
  uint32_t cap;
  uint32_t len;
  bool in;        /**< Between an END that started a packet and the one that ends it */
  bool esc;       /**< The last byte was ESC */
  bool overflow;  /**< Longer than `buf`: dropped at its END */
} sys_flasher_slip_rx_t;

/**
 * @brief SLIP-encodes `len` bytes: END, the bytes with 0xC0 and 0xDB escaped, END.
 * @param out  At least SYS_FLASHER_SLIP_MAX(len) bytes.
 * @return Bytes written.
 */
uint32_t sys_flasher_slip_encode(const uint8_t* in, uint32_t len, uint8_t* out);

/** @brief Starts a decoder on `buf` (`cap` bytes); bytes outside a packet (the ROM's boot text) are skipped. */
void sys_flasher_slip_start(sys_flasher_slip_rx_t* rx, uint8_t* buf, uint32_t cap);

/**
 * @brief Feeds one byte.
 * @return true when it ended a whole packet: its `rx->len` bytes are in `rx->buf` until the next feed.
 */
bool sys_flasher_slip_feed(sys_flasher_slip_rx_t* rx, uint8_t byte);

/** @brief FLASH_DATA's checksum: the data's bytes XORed onto SYS_FLASHER_ROM_SEED. */
uint32_t sys_flasher_rom_checksum(const uint8_t* data, uint32_t len);

/**
 * @brief A command's packet, before SLIP: [0x00][op][len u16][checksum u32][data].
 * @param out  At least SYS_FLASHER_ROM_HEAD + len bytes.
 * @return Bytes written.
 */
uint32_t sys_flasher_rom_command(uint8_t op, const uint8_t* data, uint32_t len, uint32_t checksum, uint8_t* out);

/**
 * @brief Parses a decoded packet as an answer: [0x01][op][size u16][value u32][data][status][error][0][0].
 * @return false when it is no answer (a command echoed, too short, a size that does not match).
 */
bool sys_flasher_rom_parse(const uint8_t* packet, uint32_t len, sys_flasher_rom_answer_t* out);

/** @brief Writes `digest` (16 bytes) as 32 lowercase hex digits, the form SPI_FLASH_MD5 of the ROM answers in. */
void sys_flasher_md5_hex(const uint8_t digest[16], char out[32]);

#ifdef __cplusplus
}
#endif
