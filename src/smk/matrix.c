#include "matrix.h"
#include "report.h"
#include "debug.h"
#include "layout.h"
#include "dynamic_keymap.h"
#include "user_layout.h"
#include "kb.h"
#include "user_matrix.h"
#include "kbdef.h"
#include "host.h"
#include "delay.h"
#include "indicators.h"
#include "sleep.h"
#include <stdlib.h>
#include <stdbool.h>

typedef uint8_t matrix_col_t;

matrix_col_t matrix[MATRIX_COLS];
matrix_col_t matrix_previous[MATRIX_COLS];

volatile bool matrix_updated;

// Momentary layers stack: bit n set while an MO(n) key is held. Lookups take the
// highest active layer whose entry isn't transparent, falling through to the
// default layer, so MO keys work from any layer (QMK semantics).
#define LAYER_STATE_BITS 8
static uint8_t layer_state;

// What each key resolved to when it went down, so its release undoes exactly that
// even if the active layers changed in between.
static __xdata uint16_t pressed_keycode[MATRIX_ROWS][MATRIX_COLS];

uint8_t action_layer; // highest active momentary layer (0 = none); kept for boards that read it

uint8_t default_layer;

void matrix_init()
{
    action_layer   = 0;
    default_layer  = 0;
    layer_state    = 0;
    matrix_updated = false;

    for (int i = 0; i < MATRIX_COLS; i++) {
        matrix[i]          = 0;
        matrix_previous[i] = 0;
    }
}

void set_default_layer(uint8_t layer)
{
    default_layer = layer;
}

static uint16_t resolve_keycode(uint8_t row, uint8_t col)
{
    for (uint8_t layer = LAYER_STATE_BITS - 1; layer > 0; layer--) {
        if (layer_state & (uint8_t)(1u << layer)) {
            const uint16_t kc = keymap_get(layer, row, col);
            if (kc != KC_TRANSPARENT) {
                return kc;
            }
        }
    }
    return keymap_get(default_layer, row, col);
}

static void update_action_layer(void)
{
    action_layer = 0;
    for (uint8_t layer = LAYER_STATE_BITS - 1; layer > 0; layer--) {
        if (layer_state & (uint8_t)(1u << layer)) {
            action_layer = layer;
            return;
        }
    }
}

static void send_keycode(uint16_t qcode, bool pressed)
{
    if (IS_MODIFIER_KEYCODE(qcode)) {
        if (pressed) {
            add_mods(MOD_BIT((uint8_t)(qcode & 0xFF)));
        } else {
            del_mods(MOD_BIT((uint8_t)(qcode & 0xFF)));
        }
        send_keyboard_report();
        return;
    }

    if (IS_BASIC_KEYCODE(qcode)) {
        if (pressed) {
            add_key((uint8_t)(qcode & 0xFF));
        } else {
            del_key((uint8_t)(qcode & 0xFF));
        }
        send_keyboard_report();
        return;
    }

    if (IS_SYSTEM_KEYCODE(qcode)) {
        host_system_send(pressed ? keycode_to_system(qcode) : 0);
        return;
    }

    if (IS_CONSUMER_KEYCODE(qcode)) {
        host_consumer_send(pressed ? keycode_to_consumer(qcode) : 0);
        return;
    }
}

static void process_key_state(uint8_t row, uint8_t col, bool pressed)
{
    uint16_t qcode;
    if (pressed) {
        qcode                     = resolve_keycode(row, col);
        pressed_keycode[row][col] = qcode;
    } else {
        qcode = pressed_keycode[row][col];
    }

    if (IS_QK_MOMENTARY(qcode)) {
        const uint8_t layer = QK_MOMENTARY_GET_LAYER(qcode);
        if (layer > 0 && layer < LAYER_STATE_BITS) {
            if (pressed) {
                layer_state |= (uint8_t)(1u << layer);
            } else {
                layer_state &= (uint8_t)~(1u << layer);
            }
            update_action_layer();
        }
        return;
    }

    if (!kb_process_record(qcode, pressed)) {
        return;
    }

    if (!layout_process_record(qcode, pressed)) {
        return;
    }

    send_keycode(qcode, pressed);
}

void matrix_scan_full(void)
{
    indicators_pwm_disable();

    user_matrix_sinks_off();

    user_matrix_scan_pre();
    user_matrix_cols_deselect_all();

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        user_matrix_col_select(col);

        delay_us(10); // let the row lines settle before sampling
        const uint8_t sample1 = user_matrix_read_rows();
        delay_us(10);
        const uint8_t sample2 = user_matrix_read_rows();

        if (sample1 == sample2) {
            matrix[col] = ~sample1;
        }

        user_matrix_col_deselect(col);
    }

    user_matrix_scan_post();

    indicators_pwm_enable();

    matrix_updated = true;
}

uint8_t matrix_task()
{
    if (!matrix_updated) {
        return false;
    }

    // Snapshot the scan-written matrix[], then diff it against
    // matrix_previous[]. No lock needed: each column byte reads atomically, so a
    // concurrent scan lands cleanly on one side of the read - at worst a
    // transition is split across two main-loop iterations, never lost.
    matrix_col_t snapshot[MATRIX_COLS];
    matrix_updated = false;
    for (uint8_t i = 0; i < MATRIX_COLS; i++) {
        snapshot[i] = matrix[i];
    }

    bool matrix_changed = false;

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        const matrix_col_t current_col = snapshot[col];
        const matrix_col_t col_changes = current_col ^ matrix_previous[col];
        if (!col_changes) {
            continue;
        }
        matrix_changed = true;
        sleep_note_activity(); // a key changed state; reset the inactivity timer

        matrix_col_t row_mask = 1;
        for (uint8_t row = 0; row < MATRIX_ROWS; row++, row_mask <<= 1) {
            if (col_changes & row_mask) {
                const bool key_pressed = current_col & row_mask;
                process_key_state(row, col, key_pressed);
            }
        }

        matrix_previous[col] = current_col;
    }

    return matrix_changed;
}
