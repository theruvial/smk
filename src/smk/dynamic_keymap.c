#include "dynamic_keymap.h"

#ifdef DYNAMIC_KEYMAP_ENABLE

#    include "flash.h"
#    include "watchdog.h"
#    include "keycodes.h"
#    include <string.h>

// Store layout at FLASH_KEYMAP_ADDR (FLASH_KEYMAP_SIZE bytes, whole sectors):
//   [0..5]  'D' 'K' version rows cols layers
//   [6..]   keymap, little-endian uint16 per key, layer-major
//   [..+2]  16-bit sum of the keymap bytes, written last so a torn save reads invalid
#    define STORE_VERSION 1u
#    define HDR_LEN       6u
#    define DATA_LEN      ((uint16_t)DYNAMIC_KEYMAP_LAYERS * MATRIX_ROWS * MATRIX_COLS * 2u)
#    define SUM_AT        (FLASH_KEYMAP_ADDR + HDR_LEN + DATA_LEN)
#    define CHUNK         128u

_Static_assert(HDR_LEN + DATA_LEN + 2u <= FLASH_KEYMAP_SIZE, "keymap store too small for the keymap");
_Static_assert((FLASH_KEYMAP_ADDR % FLASH_CFG_SIZE) == 0, "keymap store must start on a sector boundary");
_Static_assert((FLASH_KEYMAP_SIZE % FLASH_CFG_SIZE) == 0, "keymap store must be whole sectors");

static __xdata uint16_t dyn_keymap[DYNAMIC_KEYMAP_LAYERS][MATRIX_ROWS][MATRIX_COLS];
static __xdata uint8_t  hdr[HDR_LEN];

#    define DATA_BYTES ((__xdata uint8_t *)dyn_keymap)

static void fill_header(void)
{
    hdr[0] = 'D';
    hdr[1] = 'K';
    hdr[2] = STORE_VERSION;
    hdr[3] = MATRIX_ROWS;
    hdr[4] = MATRIX_COLS;
    hdr[5] = DYNAMIC_KEYMAP_LAYERS;
}

static uint16_t data_sum(void)
{
    uint16_t sum = 0;
    for (uint16_t i = 0; i < DATA_LEN; i++) {
        sum += DATA_BYTES[i];
    }
    return sum;
}

static void load_defaults(void)
{
    for (uint8_t l = 0; l < DYNAMIC_KEYMAP_LAYERS; l++) {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                dyn_keymap[l][r][c] = keymaps[l][r][c];
            }
        }
    }
}

static bool header_matches(void)
{
    fill_header();
    return flash_matches(FLASH_CODE, FLASH_KEYMAP_ADDR, hdr, HDR_LEN);
}

static bool load_from_flash(void)
{
    if (!header_matches()) {
        return false;
    }

    for (uint16_t off = 0; off < DATA_LEN; off += CHUNK) {
        uint8_t n = (DATA_LEN - off > CHUNK) ? CHUNK : (uint8_t)(DATA_LEN - off);
        flash_read_into(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + off, DATA_BYTES + off, n);
    }

    flash_read_into(FLASH_CODE, SUM_AT, hdr, 2); // hdr reused as a 2-byte buffer
    return ((uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8)) == data_sum();
}

void dynamic_keymap_init(void)
{
    if (!load_from_flash()) {
        load_defaults();
    }
}

uint16_t keymap_get(uint8_t layer, uint8_t row, uint8_t col)
{
    // MO() can name any layer up to 31; a missing layer is see-through.
    if (layer >= DYNAMIC_KEYMAP_LAYERS) {
        return KC_TRANSPARENT;
    }
    return dyn_keymap[layer][row][col];
}

void keymap_set(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode)
{
    dyn_keymap[layer][row][col] = keycode;
}

static bool flash_holds_current(void)
{
    if (!header_matches()) {
        return false;
    }
    for (uint16_t off = 0; off < DATA_LEN; off += CHUNK) {
        uint8_t n = (DATA_LEN - off > CHUNK) ? CHUNK : (uint8_t)(DATA_LEN - off);
        if (!flash_matches(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + off, DATA_BYTES + off, n)) {
            return false;
        }
    }
    uint16_t sum = data_sum();
    hdr[0]       = (uint8_t)sum;
    hdr[1]       = (uint8_t)(sum >> 8);
    return flash_matches(FLASH_CODE, SUM_AT, hdr, 2);
}

void dynamic_keymap_save(void)
{
    if (flash_holds_current()) {
        return; // spare the flash a needless erase cycle
    }

    for (uint16_t a = 0; a < FLASH_KEYMAP_SIZE; a += FLASH_CFG_SIZE) {
        watchdog_kick();
        flash_erase(FLASH_CODE, FLASH_KEYMAP_ADDR + a);
    }

    fill_header();
    flash_program_from(FLASH_CODE, FLASH_KEYMAP_ADDR, hdr, HDR_LEN);

    for (uint16_t off = 0; off < DATA_LEN; off += CHUNK) {
        watchdog_kick();
        uint8_t n = (DATA_LEN - off > CHUNK) ? CHUNK : (uint8_t)(DATA_LEN - off);
        flash_program_from(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + off, DATA_BYTES + off, n);
    }

    uint16_t sum = data_sum();
    hdr[0]       = (uint8_t)sum;
    hdr[1]       = (uint8_t)(sum >> 8);
    flash_program_from(FLASH_CODE, SUM_AT, hdr, 2);
}

void dynamic_keymap_reset(void)
{
    load_defaults();
    dynamic_keymap_save();
}

#endif // DYNAMIC_KEYMAP_ENABLE
