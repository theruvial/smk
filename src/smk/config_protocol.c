#include "config_protocol.h"

#ifdef DYNAMIC_KEYMAP_ENABLE

#    include "dynamic_keymap.h"
#    include "keycodes.h"
#    include "kbdef.h"
#    include "settings.h"
#    include "indicators.h"
#    include <stdbool.h>

extern uint8_t matrix[MATRIX_COLS];
extern uint8_t action_layer; // matrix.c: highest held momentary layer

static __xdata uint8_t request[CONFIG_PAYLOAD_SIZE];
static __xdata uint8_t response[CONFIG_PAYLOAD_SIZE];
static __xdata uint8_t working[CONFIG_PAYLOAD_SIZE]; // main-loop copy of request
static volatile __bit  request_pending;

void config_receive(const __xdata uint8_t *payload)
{
    for (uint8_t i = 0; i < CONFIG_PAYLOAD_SIZE; i++) {
        request[i] = payload[i];
    }
    request_pending = 1;
}

void config_read_response(uint8_t *dst)
{
    for (uint8_t i = 0; i < CONFIG_PAYLOAD_SIZE; i++) {
        dst[i] = response[i];
    }
}

static bool in_matrix(uint8_t layer, uint8_t row, uint8_t col)
{
    return layer < DYNAMIC_KEYMAP_LAYERS && row < MATRIX_ROWS && col < MATRIX_COLS;
}

// What a host may write: a real key position, and a keycode SMK knows how to act on.
static uint8_t check_set(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode)
{
    if (!in_matrix(layer, row, col)) {
        return CONFIG_ERR_RANGE;
    }
    if (keymaps[0][row][col] == KC_NO) {
        return CONFIG_ERR_NO_KEY; // no switch at this position on the board
    }
    if (keycode >= QK_USER) {
#    ifdef DYNAMIC_KEYMAP_CUSTOM_END
        // the board's own custom codes (lighting, connection, ...) are assignable
        if (keycode < (uint16_t)DYNAMIC_KEYMAP_CUSTOM_END) {
            return CONFIG_OK;
        }
#    endif
        return CONFIG_ERR_KEYCODE;
    }
    return CONFIG_OK;
}

// A boot key is unused (0xFF) or a real key position; used ones must be distinct.
static uint8_t check_boot_keys(const __xdata uint8_t *k)
{
    for (uint8_t i = 0; i < 3; i++) {
        if (k[i] == BOOT_KEY_NONE) {
            continue;
        }
        const uint8_t row = k[i] >> 4, col = k[i] & 0x0F;
        if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
            return CONFIG_ERR_RANGE;
        }
        if (keymaps[0][row][col] == KC_NO) {
            return CONFIG_ERR_NO_KEY;
        }
        for (uint8_t j = 0; j < i; j++) {
            if (k[j] == k[i]) {
                return CONFIG_ERR_RANGE;
            }
        }
    }
    return CONFIG_OK;
}

static uint8_t run(__xdata uint8_t *req, uint8_t *out)
{
    const uint8_t layer = req[2], row = req[3], col = req[4];

    switch (req[0]) {
        case CONFIG_CMD_GET_INFO:
            out[0] = CONFIG_PROTOCOL_VER;
            out[1] = MATRIX_ROWS;
            out[2] = MATRIX_COLS;
            out[3] = DYNAMIC_KEYMAP_LAYERS;
            return CONFIG_OK;

        case CONFIG_CMD_GET_KEYCODES: {
            if (!in_matrix(layer, row, col)) {
                return CONFIG_ERR_RANGE;
            }
            uint16_t a = keymap_get(layer, row, col);
            uint16_t b = (col + 1u < MATRIX_COLS) ? keymap_get(layer, row, col + 1u) : KC_NO;
            out[0]     = (uint8_t)a;
            out[1]     = (uint8_t)(a >> 8);
            out[2]     = (uint8_t)b;
            out[3]     = (uint8_t)(b >> 8);
            return CONFIG_OK;
        }

        case CONFIG_CMD_SET_KEYCODE: {
            uint16_t kc     = (uint16_t)req[5] | ((uint16_t)req[6] << 8);
            uint8_t  status = check_set(layer, row, col, kc);
            if (status == CONFIG_OK) {
                keymap_set(layer, row, col, kc);
            }
            return status;
        }

        case CONFIG_CMD_RESET_KEYMAP:
            settings_save_pre(); // flash erase runs with IRQs off: park scan/LEDs like a settings save
            dynamic_keymap_reset();
            settings_save_post();
            return CONFIG_OK;

        case CONFIG_CMD_GET_MATRIX: {
            uint8_t c = req[2];
            if (c >= MATRIX_COLS) {
                return CONFIG_ERR_RANGE;
            }
            out[0] = matrix[c];
            out[1] = (c + 1u < MATRIX_COLS) ? matrix[c + 1u] : 0;
            out[2] = action_layer; // so a key tester can label keys for the layer in use
            return CONFIG_OK;
        }

        case CONFIG_CMD_SAVE:
            settings_save_pre();
            dynamic_keymap_save();
            settings_save_post();
            return CONFIG_OK;

        case CONFIG_CMD_GET_BOOT_KEYS:
            out[0] = user_settings.boot_keys[0];
            out[1] = user_settings.boot_keys[1];
            out[2] = user_settings.boot_keys[2];
            return CONFIG_OK;

        case CONFIG_CMD_SET_BOOT_KEYS: {
            uint8_t status = check_boot_keys(&req[2]);
            if (status == CONFIG_OK) {
                user_settings.boot_keys[0] = req[2];
                user_settings.boot_keys[1] = req[3];
                user_settings.boot_keys[2] = req[4];
                settings_mark_dirty(); // saved by settings_task()
            }
            return status;
        }

        case CONFIG_CMD_GET_LIGHTING:
            out[0] = user_settings.led_effect;
            out[1] = user_settings.led_brightness;
            out[2] = user_settings.led_speed;
            return CONFIG_OK;

        case CONFIG_CMD_SET_LIGHTING:
            user_settings.led_effect     = req[2];
            user_settings.led_brightness = req[3];
            user_settings.led_speed      = req[4];
            indicators_validate_settings(); // the board clamps effect/speed to what it supports
            settings_mark_dirty();
            return CONFIG_OK;

        case CONFIG_CMD_GET_LAYER_COLOR: {
            const uint8_t l = req[2];
            if (l < 1u || l > 3u) { // the base layer has no highlight
                return CONFIG_ERR_RANGE;
            }
            out[0] = (user_settings.layer_hl >> l) & 1u;
            out[1] = user_settings.layer_rgb[l - 1u][0];
            out[2] = user_settings.layer_rgb[l - 1u][1];
            out[3] = user_settings.layer_rgb[l - 1u][2];
            return CONFIG_OK;
        }

        case CONFIG_CMD_SET_LAYER_COLOR: {
            const uint8_t l = req[2];
            if (l < 1u || l > 3u) {
                return CONFIG_ERR_RANGE;
            }
            if (req[3]) {
                user_settings.layer_hl |= (uint8_t)(1u << l);
            } else {
                user_settings.layer_hl &= (uint8_t)~(1u << l);
            }
            user_settings.layer_rgb[l - 1u][0] = req[4];
            user_settings.layer_rgb[l - 1u][1] = req[5];
            user_settings.layer_rgb[l - 1u][2] = req[6];
            settings_mark_dirty();
            return CONFIG_OK;
        }

        case CONFIG_CMD_GET_KEY_RGB: {
            if (!in_matrix(layer, row, col)) {
                return CONFIG_ERR_RANGE;
            }
            const __xdata uint8_t *rgb = keycolor_rgb(layer, row, col);
            out[0] = rgb[0];
            out[1] = rgb[1];
            out[2] = rgb[2];
            out[3] = keycolor_is_auto(layer, row, col) ? 1u : 0u;
            return CONFIG_OK;
        }

        case CONFIG_CMD_SET_KEY_RGB: {
            const uint8_t l = req[2] >> 4, r = req[2] & 0x0F, c = req[3];
            if (!in_matrix(l, r, c)) {
                return CONFIG_ERR_RANGE;
            }
            if (keymaps[0][r][c] == KC_NO) {
                return CONFIG_ERR_NO_KEY;
            }
            keycolor_set_rgb(l, r, c, req[4], req[5], req[6]);
            return CONFIG_OK;
        }

        case CONFIG_CMD_SET_KEY_AUTO:
            if (!in_matrix(layer, row, col)) {
                return CONFIG_ERR_RANGE;
            }
            keycolor_set_auto(layer, row, col);
            return CONFIG_OK;

        case CONFIG_CMD_GET_SOLID_RGB:
            out[0] = user_settings.solid_rgb[0];
            out[1] = user_settings.solid_rgb[1];
            out[2] = user_settings.solid_rgb[2];
            return CONFIG_OK;

        case CONFIG_CMD_SET_SOLID_RGB:
            user_settings.solid_rgb[0] = req[2];
            user_settings.solid_rgb[1] = req[3];
            user_settings.solid_rgb[2] = req[4];
            settings_mark_dirty();
            return CONFIG_OK;

        case CONFIG_CMD_RESET_COLORS:
            settings_save_pre();
            dynamic_keymap_reset_colors();
            settings_save_post();
            return CONFIG_OK;

        default:
            return CONFIG_ERR_CMD;
    }
}

void config_task(void)
{
    if (!request_pending) {
        return;
    }

    __critical
    {
        for (uint8_t i = 0; i < CONFIG_PAYLOAD_SIZE; i++) {
            working[i] = request[i];
        }
        request_pending = 0;
    }

    uint8_t data[4] = {0, 0, 0, 0};
    uint8_t status  = run(working, data);

    // published atomically so a GET_REPORT never sees half a response
    __critical
    {
        response[0] = working[0];
        response[1] = working[1];
        response[2] = status;
        response[3] = data[0];
        response[4] = data[1];
        response[5] = data[2];
        response[6] = data[3];
    }
}

#endif // DYNAMIC_KEYMAP_ENABLE
