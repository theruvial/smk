#include "dynamic_keymap.h"

#ifdef DYNAMIC_KEYMAP_ENABLE

#    include "flash.h"
#    include "watchdog.h"
#    include "keycodes.h"
#    include "led_effect.h"
#    include <string.h>

// Store layout at FLASH_KEYMAP_ADDR (FLASH_KEYMAP_SIZE bytes, whole sectors):
//   [0..5]  'D' 'K' version rows cols layers
//   [6..]   payload, then a 16-bit sum of the payload bytes (written last, so a torn
//           save reads invalid). Payload by version:
//     v1  keymap                                  (uint16 LE per key, layer-major)
//     v2  keymap, colour index per key            (wheel index / 0xFF auto / 0xFE off / 0xFD white)
//     v3  keymap, RGB per key, auto bitmap        (3 bytes per key; bit set = key is auto)
// Older stores load and convert; saves always write v3.
#    define STORE_VERSION 3u
#    define HDR_LEN       6u
#    define KEYS          ((uint16_t)DYNAMIC_KEYMAP_LAYERS * MATRIX_ROWS * MATRIX_COLS)
#    define MAP_LEN       (KEYS * 2u)
#    define AUTO_LEN      ((KEYS + 7u) / 8u)
#    define DATA_LEN      (MAP_LEN + KEYS * 3u + AUTO_LEN)
#    define V2_LEN        (MAP_LEN + KEYS)
#    define CHUNK         128u

_Static_assert(HDR_LEN + DATA_LEN + 2u <= FLASH_KEYMAP_SIZE, "keymap store too small for the keymap and colours");
_Static_assert((FLASH_KEYMAP_ADDR % FLASH_CFG_SIZE) == 0, "keymap store must start on a sector boundary");
_Static_assert((FLASH_KEYMAP_SIZE % FLASH_CFG_SIZE) == 0, "keymap store must be whole sectors");

// keymap, colours and auto flags back to back: the payload is one contiguous range
static __xdata struct {
    uint16_t map[DYNAMIC_KEYMAP_LAYERS][MATRIX_ROWS][MATRIX_COLS];
    uint8_t  rgb[DYNAMIC_KEYMAP_LAYERS][MATRIX_ROWS][MATRIX_COLS][3];
    uint8_t  autobits[AUTO_LEN];
} store;
static __xdata uint8_t hdr[HDR_LEN];

#    define DATA_BYTES ((__xdata uint8_t *)&store)

_Static_assert(sizeof(store) == DATA_LEN, "store struct must be packed");

static uint16_t key_index(uint8_t layer, uint8_t row, uint8_t col)
{
    return ((uint16_t)layer * MATRIX_ROWS + row) * MATRIX_COLS + col;
}

static void fill_header(uint8_t version)
{
    hdr[0] = 'D';
    hdr[1] = 'K';
    hdr[2] = version;
    hdr[3] = MATRIX_ROWS;
    hdr[4] = MATRIX_COLS;
    hdr[5] = DYNAMIC_KEYMAP_LAYERS;
}

static uint16_t data_sum(uint16_t len)
{
    uint16_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += DATA_BYTES[i];
    }
    return sum;
}

static void read_range(uint16_t off, uint16_t len)
{
    for (uint16_t done = 0; done < len; done += CHUNK) {
        uint8_t n = (len - done > CHUNK) ? CHUNK : (uint8_t)(len - done);
        flash_read_into(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + off + done, DATA_BYTES + off + done, n);
    }
}

static void map_defaults(void)
{
    for (uint8_t l = 0; l < DYNAMIC_KEYMAP_LAYERS; l++) {
        for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
            for (uint8_t c = 0; c < MATRIX_COLS; c++) {
                store.map[l][r][c] = keymaps[l][r][c];
            }
        }
    }
}

static void color_defaults(void)
{
    memset(store.rgb, 0, sizeof(store.rgb));
    memset(store.autobits, 0xFF, sizeof(store.autobits));
}

bool keycolor_is_auto(uint8_t layer, uint8_t row, uint8_t col)
{
    if (layer >= DYNAMIC_KEYMAP_LAYERS) {
        return true;
    }
    const uint16_t i = key_index(layer, row, col);
    return (store.autobits[i >> 3] & (uint8_t)(1u << (i & 7u))) != 0;
}

static void set_auto(uint16_t i, bool on)
{
    if (on) {
        store.autobits[i >> 3] |= (uint8_t)(1u << (i & 7u));
    } else {
        store.autobits[i >> 3] &= (uint8_t)~(1u << (i & 7u));
    }
}

// v2 kept one colour-index byte per key right after the keymap, where v3's RGB
// block now starts. Convert in place, back to front, so no index is overwritten
// before it's read (each key's 3 RGB bytes land at or after its v2 byte).
static void convert_v2_colors(void)
{
    __xdata uint8_t *v2 = DATA_BYTES + MAP_LEN;
    memset(store.autobits, 0, sizeof(store.autobits));
    for (uint16_t k = KEYS; k-- > 0;) {
        const uint8_t idx = v2[k];
        __xdata uint8_t *dst = &store.rgb[0][0][0][0] + k * 3u;
        if (idx == 0xFFu) { // auto
            dst[0] = dst[1] = dst[2] = 0;
            set_auto(k, true);
        } else if (idx == 0xFEu) { // off
            dst[0] = dst[1] = dst[2] = 0;
        } else if (idx == 0xFDu) { // white
            dst[0] = dst[1] = dst[2] = 255;
        } else {
            uint8_t rgb[3];
            led_color_wheel(idx, rgb);
            dst[0] = rgb[0];
            dst[1] = rgb[1];
            dst[2] = rgb[2];
        }
    }
}

static bool load_version(uint8_t version, uint16_t len)
{
    fill_header(version);
    if (!flash_matches(FLASH_CODE, FLASH_KEYMAP_ADDR, hdr, HDR_LEN)) {
        return false;
    }
    read_range(0, len);
    flash_read_into(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + len, hdr, 2); // hdr reused as a 2-byte buffer
    return ((uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8)) == data_sum(len);
}

static bool load_from_flash(void)
{
    if (load_version(STORE_VERSION, DATA_LEN)) {
        return true;
    }
    if (load_version(2, V2_LEN)) {
        convert_v2_colors();
        return true;
    }
    if (load_version(1, MAP_LEN)) {
        color_defaults();
        return true;
    }
    return false;
}

void dynamic_keymap_init(void)
{
    if (!load_from_flash()) {
        map_defaults();
        color_defaults();
    }
}

uint16_t keymap_get(uint8_t layer, uint8_t row, uint8_t col)
{
    // MO() can name any layer up to 31; a missing layer is see-through.
    if (layer >= DYNAMIC_KEYMAP_LAYERS) {
        return KC_TRANSPARENT;
    }
    return store.map[layer][row][col];
}

void keymap_set(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode)
{
    store.map[layer][row][col] = keycode;
}

const __xdata uint8_t *keycolor_rgb(uint8_t layer, uint8_t row, uint8_t col)
{
    return store.rgb[layer][row][col];
}

void keycolor_set_rgb(uint8_t layer, uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b)
{
    store.rgb[layer][row][col][0] = r;
    store.rgb[layer][row][col][1] = g;
    store.rgb[layer][row][col][2] = b;
    set_auto(key_index(layer, row, col), false);
}

void keycolor_set_auto(uint8_t layer, uint8_t row, uint8_t col)
{
    set_auto(key_index(layer, row, col), true);
}

static bool flash_holds_current(void)
{
    fill_header(STORE_VERSION);
    if (!flash_matches(FLASH_CODE, FLASH_KEYMAP_ADDR, hdr, HDR_LEN)) {
        return false;
    }
    for (uint16_t off = 0; off < DATA_LEN; off += CHUNK) {
        uint8_t n = (DATA_LEN - off > CHUNK) ? CHUNK : (uint8_t)(DATA_LEN - off);
        if (!flash_matches(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + off, DATA_BYTES + off, n)) {
            return false;
        }
    }
    uint16_t sum = data_sum(DATA_LEN);
    hdr[0]       = (uint8_t)sum;
    hdr[1]       = (uint8_t)(sum >> 8);
    return flash_matches(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + DATA_LEN, hdr, 2);
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

    fill_header(STORE_VERSION);
    flash_program_from(FLASH_CODE, FLASH_KEYMAP_ADDR, hdr, HDR_LEN);

    for (uint16_t off = 0; off < DATA_LEN; off += CHUNK) {
        watchdog_kick();
        uint8_t n = (DATA_LEN - off > CHUNK) ? CHUNK : (uint8_t)(DATA_LEN - off);
        flash_program_from(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + off, DATA_BYTES + off, n);
    }

    uint16_t sum = data_sum(DATA_LEN);
    hdr[0]       = (uint8_t)sum;
    hdr[1]       = (uint8_t)(sum >> 8);
    flash_program_from(FLASH_CODE, FLASH_KEYMAP_ADDR + HDR_LEN + DATA_LEN, hdr, 2);
}

void dynamic_keymap_reset(void)
{
    map_defaults();
    dynamic_keymap_save();
}

void dynamic_keymap_reset_colors(void)
{
    color_defaults();
    dynamic_keymap_save();
}

#endif // DYNAMIC_KEYMAP_ENABLE
