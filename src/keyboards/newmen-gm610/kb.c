#include <stdint.h>
#include <stdbool.h>
#include "keycodes.h"
#include "kbdef.h"
#include "keyboard.h"
#include "layout.h"
#include "settings.h"
#include "debug.h"
#include "report.h"
#include "usb.h"

#ifdef RF_ENABLED
#    include "rf_controller.h"
#endif
#ifdef ISP_ENABLE
#    include "isp.h"
#endif

#include "delay.h"

extern uint8_t matrix[MATRIX_COLS]; // src/smk/matrix.c: per column, bit r = row r pressed

// The GM610 has no USB/RF slider (stock switches with Fn combos), so the connection
// mode is chosen by keys and persisted in user_settings.conn_mode. USB stays
// enumerated whenever a cable is in, so the ISP reflash path works in any mode.

typedef enum {
    KEYBOARD_CONN_MODE_RF  = 0,
    KEYBOARD_CONN_MODE_USB = 1,
} user_keyboard_conn_mode_t;

static user_keyboard_conn_mode_t conn_mode = KEYBOARD_CONN_MODE_USB;
static bool                      mode_applied;

#define STATUS_FRAMES 50 // ~2 s

static bool boot_chord_held(void)
{
    const bool esc = (matrix[BOOT_KEY_COL] & (1 << BOOT_KEY_ROW)) != 0;
    const bool smk = (matrix[BOOT_CHORD_S_COL] & (1 << 2)) && (matrix[BOOT_CHORD_M_COL] & (1 << 3)) && (matrix[BOOT_CHORD_K_COL] & (1 << 2));
    return esc || smk;
}

// Failsafe: Esc (or S + M + K) held while plugging in -> stock ISP bootloader, so sinowisp can
// reflash even a build whose own USB or later startup is broken. kb_init() runs right
// after init() has started the matrix scan (tick ISR) and before RF/USB-wait, so this
// depends on as little of the firmware as possible. Read from the raw matrix, so it
// works whatever the keymap says.
void kb_init()
{
    delay_ms(40); // a few dozen scans, past the scan's two-sample settle
    if (boot_chord_held()) {
        delay_ms(200);
        if (boot_chord_held()) { // still held: not a bounce
#ifdef ISP_ENABLE
            isp_jump(); // the bootloader never returns
#endif
        }
    }
}

void kb_update_switches() {}

static void show_status(void)
{
    if (conn_mode == KEYBOARD_CONN_MODE_USB) {
        indicators_show_key(STATUS_ROW_TAB, STATUS_COL_TAB, 255, 255, 255, false, STATUS_FRAMES);
        return;
    }
#ifdef RF_ENABLED
    const uint8_t link    = user_settings.rf_link;
    const bool    pairing = !keyboard_state.paired;
    if (link == RF_MODE_2_4G) {
        indicators_show_key(STATUS_ROW_BT, STATUS_COL_24G, 0, 255, 0, pairing, STATUS_FRAMES);
    } else {
        indicators_show_key(STATUS_ROW_BT, (uint8_t)(STATUS_COL_BT1 + link - RF_MODE_BT1), 0, 0, 255, pairing, STATUS_FRAMES);
    }
#endif
}

static void kb_apply_conn_mode(user_keyboard_conn_mode_t mode)
{
    conn_mode = mode;

    switch (mode) {
        case KEYBOARD_CONN_MODE_USB:
            dprintf("USB_MODE\r\n");
#ifdef RF_ENABLED
            rf_apply_usb_mode();
#endif
            break;
        case KEYBOARD_CONN_MODE_RF:
            dprintf("RF_MODE\r\n");
#ifdef RF_ENABLED
            rf_set_link((rf_mode_t)user_settings.rf_link);
            rf_kbd_lazy_state_init();
#endif
            break;
    }
    show_status();
}

static void set_conn_mode(user_keyboard_conn_mode_t mode)
{
    if (user_settings.conn_mode != (uint8_t)mode) {
        user_settings.conn_mode = (uint8_t)mode;
        settings_mark_dirty();
    }
    kb_apply_conn_mode(mode);
}

#ifdef RF_ENABLED
#    define LINK_PAIRING_HOLD_TICKS 60000
static uint16_t link_hold_ticks    = 0;
static uint16_t link_hold_keycode  = 0;
static bool     link_pairing_armed = false;

// LNK_BT1..LNK_BT3 are consecutive, as are RF_MODE_BT1..RF_MODE_BT3.
static rf_mode_t kb_keycode_to_rf_mode(uint16_t keycode)
{
    if (keycode >= LNK_BT1 && keycode <= LNK_BT3) {
        return (rf_mode_t)(RF_MODE_BT1 + (uint8_t)(keycode - LNK_BT1));
    }
    return RF_MODE_2_4G;
}
#endif

bool kb_process_record(uint16_t keycode, bool key_pressed)
{
    switch (keycode) {
        case LNK_USB:
            if (key_pressed) {
                set_conn_mode(KEYBOARD_CONN_MODE_USB);
            }
            return false;

        case LNK_BT1:
        case LNK_BT2:
        case LNK_BT3:
        case LNK_24G:
#ifdef RF_ENABLED
            if (key_pressed) {
                rf_mode_t mode = kb_keycode_to_rf_mode(keycode);
                dprintf("rf link selected %02x\r\n", mode);
                user_settings.rf_link = (uint8_t)mode;
                settings_mark_dirty();
                keyboard_state.rf_link   = (uint8_t)mode;
                keyboard_state.connected = 1;
                keyboard_state.paired    = 1;
                set_conn_mode(KEYBOARD_CONN_MODE_RF);
                link_hold_keycode  = keycode;
                link_hold_ticks    = 0;
                link_pairing_armed = true;
            } else if (link_hold_keycode == keycode) {
                link_hold_keycode  = 0;
                link_pairing_armed = false;
            }
#endif
            return false;

        case FX_NEXT:
            if (key_pressed) {
                indicators_next_effect();
            }
            return false;
        case FX_PREV:
            if (key_pressed) {
                indicators_prev_effect();
            }
            return false;
        case BRI_UP:
            if (key_pressed) {
                indicators_brightness_up();
            }
            return false;
        case BRI_DN:
            if (key_pressed) {
                indicators_brightness_down();
            }
            return false;
        case SPD_UP:
            if (key_pressed) {
                indicators_speed_up();
            }
            return false;
        case SPD_DN:
            if (key_pressed) {
                indicators_speed_down();
            }
            return false;

        default:
            return true;
    }
}

void kb_send_report(__xdata report_keyboard_t *report)
{
#ifdef RF_ENABLED
    if (conn_mode == KEYBOARD_CONN_MODE_RF) {
        rf_send_report(report);
        return;
    }
#endif
    usb_send_report(report);
}

void kb_send_nkro(__xdata report_nkro_t *report)
{
#ifdef RF_ENABLED
    if (conn_mode == KEYBOARD_CONN_MODE_RF) {
        rf_send_nkro(report);
        return;
    }
#endif
    usb_send_nkro(report);
}

void kb_send_extra(__xdata report_extra_t *report)
{
#ifdef RF_ENABLED
    if (conn_mode == KEYBOARD_CONN_MODE_RF) {
        rf_send_extra(report);
        return;
    }
#endif
    usb_send_extra(report);
}

void kb_update()
{

    // settings are restored after kb_init(), so the saved mode is applied here
    if (!mode_applied) {
        mode_applied = true;
#ifdef RF_ENABLED
        kb_apply_conn_mode((user_keyboard_conn_mode_t)user_settings.conn_mode);
#else
        kb_apply_conn_mode(KEYBOARD_CONN_MODE_USB);
#endif
    }

#ifdef RF_ENABLED
    if (conn_mode == KEYBOARD_CONN_MODE_RF) {
        static uint8_t last_paired = 0xFF;

        if (link_pairing_armed && link_hold_keycode) {
            if (link_hold_ticks < LINK_PAIRING_HOLD_TICKS) {
                link_hold_ticks++;
            } else {
                rf_mode_t mode = kb_keycode_to_rf_mode(link_hold_keycode);
                dprintf("rf link pairing %02x\r\n", mode);
                keyboard_state.paired    = 0;
                keyboard_state.connected = 0;
                rf_set_link_pairing(mode, &keyboard_state);
                link_pairing_armed = false;
            }
        }

        rf_link_supervisor(&keyboard_state);
        rf_send_pending_flush();
        rf_blanking_tick();

        // keep the mode key blinking for as long as pairing lasts, solid once paired
        if (keyboard_state.paired != last_paired) {
            last_paired = keyboard_state.paired;
            show_status();
        } else if (!keyboard_state.paired) {
            show_status();
        }
    }
#endif
}
