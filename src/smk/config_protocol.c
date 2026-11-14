#include "config_protocol.h"

#ifdef DYNAMIC_KEYMAP_ENABLE

#    include "dynamic_keymap.h"
#    include "keycodes.h"
#    include "kbdef.h"
#    include <stdbool.h>

extern uint8_t matrix[MATRIX_COLS];

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
        return CONFIG_ERR_KEYCODE; // board/user custom codes stay compile-time only
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
            dynamic_keymap_reset();
            return CONFIG_OK;

        case CONFIG_CMD_GET_MATRIX: {
            uint8_t c = req[2];
            if (c >= MATRIX_COLS) {
                return CONFIG_ERR_RANGE;
            }
            out[0] = matrix[c];
            out[1] = (c + 1u < MATRIX_COLS) ? matrix[c + 1u] : 0;
            return CONFIG_OK;
        }

        case CONFIG_CMD_SAVE:
            dynamic_keymap_save();
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
