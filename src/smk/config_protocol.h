#pragma once

// Host configuration protocol over a vendor HID feature report (REPORT_ID_CONFIG
// on interface 1). EP0 data stages are a single 8-byte packet, so each report is
// the report ID plus CONFIG_PAYLOAD_SIZE bytes, in both directions.
//
// Request:  [cmd, seq, a0, a1, a2, a3, a4]
// Response: [cmd, seq, status, d0, d1, d2, d3]
//
// SET_REPORT only queues the request (it arrives in the USB ISR); config_task()
// runs it from the main loop and publishes the response, which GET_REPORT reads.
// A host polls GET_REPORT until the echoed seq matches the request it sent.

#include <stdint.h>

#define CONFIG_PAYLOAD_SIZE 7u
#define CONFIG_PROTOCOL_VER 1u

enum config_cmd {
    CONFIG_CMD_GET_INFO     = 0x01, // -> ver, rows, cols, layers
    CONFIG_CMD_GET_KEYCODES = 0x02, // layer,row,col -> kc(row,col), kc(row,col+1), little-endian
    CONFIG_CMD_SET_KEYCODE  = 0x03, // layer,row,col,kc_lo,kc_hi
    CONFIG_CMD_RESET_KEYMAP = 0x04, // back to compiled defaults, saved
    CONFIG_CMD_GET_MATRIX   = 0x05, // col -> row bitmaps of col and col+1, active layer
    CONFIG_CMD_SAVE         = 0x06, // persist the keymap to flash
    CONFIG_CMD_GET_BOOT_KEYS = 0x07, // -> k0, k1, k2 ((row << 4) | col, 0xFF = unused)
    CONFIG_CMD_SET_BOOT_KEYS = 0x08, // k0, k1, k2: extra power-up bootloader keys, saved
    CONFIG_CMD_GET_LIGHTING  = 0x09, // -> effect, brightness, speed
    CONFIG_CMD_SET_LIGHTING  = 0x0A, // effect, brightness, speed: applied live, saved
    CONFIG_CMD_GET_LAYER_COLOR = 0x0B, // layer -> on, r, g, b (layer highlight)
    CONFIG_CMD_SET_LAYER_COLOR = 0x0C, // layer, on, r, g, b: saved
    CONFIG_CMD_GET_KEY_RGB     = 0x0D, // layer,row,col -> r, g, b, auto
    CONFIG_CMD_SET_KEY_RGB     = 0x0E, // (layer << 4) | row, col, r, g, b (persist with CONFIG_CMD_SAVE)
    CONFIG_CMD_RESET_COLORS    = 0x0F, // every key colour back to auto, saved
    CONFIG_CMD_SET_KEY_AUTO    = 0x10, // layer,row,col: key back to auto (persist with CONFIG_CMD_SAVE)
    CONFIG_CMD_GET_SOLID_RGB   = 0x11, // -> r, g, b of the solid effect
    CONFIG_CMD_SET_SOLID_RGB   = 0x12, // r, g, b: applied live, saved
};

enum config_status {
    CONFIG_OK          = 0x00,
    CONFIG_ERR_CMD     = 0x01,
    CONFIG_ERR_RANGE   = 0x02,
    CONFIG_ERR_KEYCODE = 0x03,
    CONFIG_ERR_NO_KEY  = 0x04,
};

#ifdef DYNAMIC_KEYMAP_ENABLE

void config_receive(const __xdata uint8_t *payload); // USB ISR: queue a request
void config_read_response(uint8_t *dst);             // USB ISR: copy out the response
void config_task(void);                              // main loop

#endif
