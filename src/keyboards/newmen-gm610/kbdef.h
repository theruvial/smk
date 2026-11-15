#pragma once

#include "sh68f90.h"
#include "keycodes.h"
#include <stdint.h>
#include <stdbool.h>

// Measured with a multimeter on PCB K948 US BYK916 V1.1 (LQFP-64 pin numbers in comments).
// Diode anode on the row, so columns are driven low and rows read with pull-ups.

#define MATRIX_ROWS 5
#define MATRIX_COLS 14

// Row Pin Bits
#define KB_R0_P7_1 _P7_1 // pin 61, Esc row
#define KB_R1_P7_2 _P7_2 // pin 60, Tab row
#define KB_R2_P7_3 _P7_3 // pin 59, Caps row
#define KB_R3_P5_3 _P5_3 // pin 13, Shift row
#define KB_R4_P5_4 _P5_4 // pin 14, Ctrl row

// Row Pins
#define KB_R0 P7_1
#define KB_R1 P7_2
#define KB_R2 P7_3
#define KB_R3 P5_3
#define KB_R4 P5_4

// Column Pin Bits
#define KB_C0_P5_0  _P5_0 // pin 10
#define KB_C1_P5_1  _P5_1 // pin 11
#define KB_C2_P5_2  _P5_2 // pin 12
#define KB_C3_P3_5  _P3_5 // pin 46
#define KB_C4_P3_4  _P3_4 // pin 45
#define KB_C5_P3_3  _P3_3 // pin 44
#define KB_C6_P3_2  _P3_2 // pin 43
#define KB_C7_P3_1  _P3_1 // pin 42
#define KB_C8_P3_0  _P3_0 // pin 41
#define KB_C9_P2_5  _P2_5 // pin 40
#define KB_C10_P2_4 _P2_4 // pin 39
#define KB_C11_P2_3 _P2_3 // pin 38
#define KB_C12_P2_2 _P2_2 // pin 37
#define KB_C13_P2_1 _P2_1 // pin 36

// Column Pins
#define KB_C0  P5_0
#define KB_C1  P5_1
#define KB_C2  P5_2
#define KB_C3  P3_5
#define KB_C4  P3_4
#define KB_C5  P3_3
#define KB_C6  P3_2
#define KB_C7  P3_1
#define KB_C8  P3_0
#define KB_C9  P2_5
#define KB_C10 P2_4
#define KB_C11 P2_3
#define KB_C12 P2_2
#define KB_C13 P2_1

// One timer alternates key scans with LED subframes (src/smk/tick.c). A frame is
// 15 subframes (5 rows x 3 colours). With 150 us subframes (led_subframe_us) and 6
// per scan: 6 x 150 + ~420 us scan = 1.32 ms per 6 subframes, so a full refresh
// is ~3.3 ms (~300 Hz) and keys are still scanned ~750 times a second.
#define LED_SUBFRAMES_PER_SCAN 6

// LED frames completed per second (~3.3 ms each), the clock for the editor's sleep timer
// (src/smk/sleep.c). Approximate: the timer is good to within a few percent.
#define SLEEP_FRAMES_PER_SEC 300

// LED PWM registers: each key column pin doubles as its LED column driver.
#define LED_PWM_C0  PWM40
#define LED_PWM_C1  PWM41
#define LED_PWM_C2  PWM42
#define LED_PWM_C3  PWM05
#define LED_PWM_C4  PWM04
#define LED_PWM_C5  PWM03
#define LED_PWM_C6  PWM02
#define LED_PWM_C7  PWM01
#define LED_PWM_C8  PWM00
#define LED_PWM_C9  PWM15
#define LED_PWM_C10 PWM14
#define LED_PWM_C11 PWM13
#define LED_PWM_C12 PWM12
#define LED_PWM_C13 PWM11

// RGB row sinks, mapped on the board with a pin-finder build (each sink raised
// alone, filmed) on 2026-11-14. Green for rows 1-2 is P6.2/P6.3 (BYK916 pins 3/4,
// "DR2_G"/"DR3_G" on the pinout); on this unit those pins light nothing, likely a
// lifted/bridged pin next to pin 2. P4.0/P4.1/P6.0 drive no LEDs (held low).
#define RGB_R0R P0_4
#define RGB_R0G P6_1
#define RGB_R0B P0_3
#define RGB_R1R P0_2
#define RGB_R1G P6_2
#define RGB_R1B P6_6
#define RGB_R2R P6_7
#define RGB_R2G P6_3
#define RGB_R2B P5_7
#define RGB_R3R P4_5
#define RGB_R3G P6_4
#define RGB_R3B P4_6
#define RGB_R4R P4_3
#define RGB_R4G P6_5
#define RGB_R4B P4_4

#define RGB_P0_MASK (uint8_t)(_P0_2 | _P0_3 | _P0_4)
#define RGB_P4_MASK (uint8_t)(_P4_0 | _P4_1 | _P4_3 | _P4_4 | _P4_5 | _P4_6) // P4.0/P4.1: unused 6th row
#define RGB_P5_MASK (uint8_t)(_P5_7)
#define RGB_P6_MASK (uint8_t)(_P6_0 | _P6_1 | _P6_2 | _P6_3 | _P6_4 | _P6_5 | _P6_6 | _P6_7) // P6.0: unused 6th row

// BK3632 bit-banged SPI: same pins as the Air60 (confirmed in the stock firmware).
// Unlike the Air60, P4.1 is an LED row here, not a wake input.
#define RF_BB_SPI_CS   P7_4
#define RF_BB_SPI_SCK  P4_7
#define RF_BB_SPI_MISO P0_6
#define RF_BB_SPI_MOSI P0_7
#define RF_BB_SPI_MOT  P0_5
#define RF_BB_SPI_ACK  P4_2

#define RF_BB_SPI_CS_P7_4   _P7_4
#define RF_BB_SPI_SCK_P4_7  _P4_7
#define RF_BB_SPI_MISO_P0_6 _P0_6
#define RF_BB_SPI_MOSI_P0_7 _P0_7
#define RF_BB_SPI_MOT_P0_5  _P0_5
#define RF_BB_SPI_ACK_P4_2  _P4_2

enum custom_keycodes {
    // connection (Fn layer)
    LNK_USB = SAFE_RANGE, // wired
    LNK_BT1,              // Bluetooth slot 1 (hold ~3 s to pair)
    LNK_BT2,
    LNK_BT3,
    LNK_24G,

    // lighting (Fn layer)
    FX_NEXT,
    FX_PREV,
    BRI_UP,
    BRI_DN,
    SPD_UP,
    SPD_DN,

    // battery level on the number row (Fn layer)
    BAT_FL,  // show the level for a few seconds
    BAT_ON,  // keep it showing (saved)
    BAT_OFF, // stop (saved)

    KB_SAFE_RANGE,
};

// The live keymap editor may assign the custom codes above (SAFE_RANGE..KB_SAFE_RANGE).
#define DYNAMIC_KEYMAP_CUSTOM_END KB_SAFE_RANGE

// Failsafe: hold Esc, or S + M + K, while plugging in to jump to the stock ISP
// bootloader, so sinowisp can reflash even if a build's own USB is broken.
// S+M+K is the backup: Esc shares column 0 (pin 10) and the others don't.
#define BOOT_KEY_ROW     0 // Esc
#define BOOT_KEY_COL     0
#define BOOT_CHORD_S_COL 2 // row 2
#define BOOT_CHORD_M_COL 7 // row 3
#define BOOT_CHORD_K_COL 8 // row 2

// Key positions (row, col) used to show connection status on the backlight.
#define STATUS_ROW_TAB 1
#define STATUS_COL_TAB 0
#define STATUS_ROW_BT  1 // Q, W, E = cols 1..3
#define STATUS_COL_BT1 1
#define STATUS_COL_24G 4 // R

void indicators_next_effect(void);
void indicators_prev_effect(void);
void indicators_brightness_up(void);
void indicators_brightness_down(void);
void indicators_speed_up(void);
void indicators_speed_down(void);
void indicators_battery_flash(void);
void indicators_battery_on(void);
void indicators_battery_off(void);
void indicators_battery_set_valid(bool valid);
void indicators_show_key(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b, bool blink, uint8_t frames);
