#include "indicators.h"
#include "kbdef.h"
#include "gpio.h"
#include "pwm.h"
#include "settings.h"
#include "tick.h"
#include "keyboard.h"
#include "led_effect.h"
#include "user_led.h"
#include "dynamic_keymap.h"
#include <string.h>

// Per-key RGB for the GM610: 5 LED rows x 14 LED columns. Adapted from the NuPhy
// Air60 driver (same LED topology) minus its underglow row; the GM610 has none.
// The LED matrix is time-multiplexed: one row and one colour sink at a time, with
// the 14 column PWM duties loaded for that subframe.

#define LED_ROWS MATRIX_ROWS
#define LED_COLS MATRIX_COLS

#define LED_SPEED_DEFAULT 2
#define LED_SPEED_MIN     1
#define LED_SPEED_MAX     16

#define LED_BRIGHTNESS_DEFAULT 255

// Default colours (RGB): solid effect purple; layer highlights teal, orange, rose.
static __code const uint8_t solid_default[3]    = {111, 0, 144};
static __code const uint8_t layer_default[3][3] = {{0, 129, 126}, {180, 75, 0}, {195, 0, 60}}; // layers 1-3
#define LAYER_HL_DEFAULT 0x0E // layers 1-3 highlight, the base layer doesn't

extern uint8_t action_layer; // src/smk/matrix.c: highest held momentary layer, 0 = none
#define LED_BRIGHTNESS_STEP    32

// fb value -> DUTY2: the LED conducts while the column is LOW, i.e. for DUTY2 cycles.
#define LED_DUTY(v) (uint16_t)(v)

#define SCALE_BRI(v) (uint8_t)(((uint16_t)(uint8_t)(v) * user_settings.led_brightness) >> 8)

#include LED_GEOMETRY_HEADER
_Static_assert(LED_GEOMETRY_ROWS == LED_ROWS && LED_GEOMETRY_COLS == LED_COLS, "generated LED geometry size does not match the key matrix");

static uint8_t led_fb[LED_ROWS][3][LED_COLS];

static uint8_t led_row;
static uint8_t led_color;
static uint8_t led_phase;
static uint8_t regen_row;
static uint8_t regen_col;
static uint8_t anim_ctr;
static uint8_t frame_ctr;

static volatile bool render_dirty;

// One key can be overridden with a status colour (connection mode / pairing).
static uint8_t status_row = 0xFF;
static uint8_t status_col;
static uint8_t status_rgb[3];
static bool    status_blink;
static uint8_t status_frames;

// Battery level (from the Bluetooth chip, 0..7) shown as a bar on the number row keys
// 1-7, lit for a few seconds or kept on. Only drawn once a reading has arrived.
#define BAT_FRAMES   100 // ~4 s
#define BAT_BAR_ROW  0
#define BAT_BAR_COL0 1
#define BAT_BAR_LEN  7
static uint8_t battery_frames;
static bool    battery_valid;

void        indicators_pwm_enable();
void        indicators_pwm_disable();
static void led_regen_one();
static void led_enable_sink();
static bool led_set_columns();

void indicators_apply_defaults()
{
    user_settings.led_effect     = FX_SOLID;
    user_settings.led_brightness = LED_BRIGHTNESS_DEFAULT;
    user_settings.led_speed      = LED_SPEED_DEFAULT;
    user_settings.ul_effect      = FX_OFF;
    user_settings.ul_brightness  = 0;
    user_settings.ul_speed       = LED_SPEED_MIN;
    user_settings.battery_indicator_on = 0;
    user_settings.rf_link        = 0; // RF_MODE_2_4G
    user_settings.conn_mode      = 1; // USB
    user_settings.boot_keys[0]   = BOOT_KEY_NONE; // no custom failsafe combo
    user_settings.boot_keys[1]   = BOOT_KEY_NONE;
    user_settings.boot_keys[2]   = BOOT_KEY_NONE;
    for (uint8_t i = 0; i < 3; i++) {
        user_settings.solid_rgb[i] = solid_default[i];
        for (uint8_t l = 0; l < 3; l++) {
            user_settings.layer_rgb[l][i] = layer_default[l][i];
        }
    }
    user_settings.layer_hl = LAYER_HL_DEFAULT;
    user_settings.sleep_min = 5; // minutes idle before Bluetooth sleep, 0 = never
}

void indicators_validate_settings()
{
    if (user_settings.led_effect > FX_OFF) {
        user_settings.led_effect = FX_OFF;
    }
    if (user_settings.led_speed < LED_SPEED_MIN) {
        user_settings.led_speed = LED_SPEED_MIN;
    }
    if (user_settings.led_speed > LED_SPEED_MAX) {
        user_settings.led_speed = LED_SPEED_MAX;
    }
    if (user_settings.rf_link > 3) {
        user_settings.rf_link = 0;
    }
    if (user_settings.conn_mode > 1) {
        user_settings.conn_mode = 1;
    }
    if (user_settings.battery_indicator_on > 1) {
        user_settings.battery_indicator_on = 0;
    }
}

void indicators_init()
{
    memset(led_fb, 0, sizeof(led_fb));
}

void indicators_start()
{
    led_row      = 0;
    led_color    = 0;
    anim_ctr     = 0;
    led_phase    = 0;
    regen_row    = 0;
    regen_col    = 0;
    render_dirty = true;

    indicators_pwm_enable();
}

void indicators_next_effect(void)
{
    if (++user_settings.led_effect > FX_OFF) {
        user_settings.led_effect = 0;
    }
    settings_mark_dirty();
}

void indicators_prev_effect(void)
{
    if (user_settings.led_effect == 0) {
        user_settings.led_effect = FX_OFF;
    } else {
        user_settings.led_effect--;
    }
    settings_mark_dirty();
}

void indicators_brightness_up(void)
{
    if (user_settings.led_brightness > (uint8_t)(255 - LED_BRIGHTNESS_STEP)) {
        user_settings.led_brightness = 255;
    } else {
        user_settings.led_brightness = (uint8_t)(user_settings.led_brightness + LED_BRIGHTNESS_STEP);
    }
    settings_mark_dirty();
}

void indicators_brightness_down(void)
{
    if (user_settings.led_brightness < LED_BRIGHTNESS_STEP) {
        user_settings.led_brightness = 0;
    } else {
        user_settings.led_brightness = (uint8_t)(user_settings.led_brightness - LED_BRIGHTNESS_STEP);
    }
    settings_mark_dirty();
}

void indicators_speed_up(void)
{
    if (user_settings.led_speed < LED_SPEED_MAX) {
        user_settings.led_speed++;
    }
    settings_mark_dirty();
}

void indicators_speed_down(void)
{
    if (user_settings.led_speed > LED_SPEED_MIN) {
        user_settings.led_speed--;
    }
    settings_mark_dirty();
}

// frames: how long to show it (~40 ms per frame); 0 clears.
void indicators_battery_flash(void)
{
    battery_frames = BAT_FRAMES;
    render_dirty   = true;
}

void indicators_battery_on(void)
{
    user_settings.battery_indicator_on = 1;
    settings_mark_dirty();
    render_dirty = true;
}

void indicators_battery_off(void)
{
    user_settings.battery_indicator_on = 0;
    battery_frames                     = 0;
    settings_mark_dirty();
    render_dirty = true;
}

void indicators_battery_set_valid(bool valid)
{
    battery_valid = valid;
    render_dirty  = true;
}

void indicators_show_key(uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b, bool blink, uint8_t frames)
{
    status_row    = frames ? row : 0xFF;
    status_col    = col;
    status_rgb[0] = r;
    status_rgb[1] = g;
    status_rgb[2] = b;
    status_blink  = blink;
    status_frames = frames;
    render_dirty  = true;
}

void indicators_pre_update()
{
    GPIO_LOW(0, RGB_P0_MASK);
    GPIO_LOW(4, RGB_P4_MASK);
    GPIO_LOW(5, RGB_P5_MASK);
    GPIO_LOW(6, RGB_P6_MASK);
}

void indicators_render()
{
    if (!render_dirty) {
        return;
    }
    render_dirty = false; // cleared first: a phase bump mid-render re-arms it

    for (uint8_t i = 0; i < (uint8_t)(LED_ROWS * LED_COLS); i++) {
        led_regen_one();
    }
}

#ifdef GM610_PIN_FINDER
// LED wiring probe through the normal PWM column path: every column at full duty,
// and one candidate row-sink pin raised per step. Each cycle: 2 dark steps, then the
// 18 pins below, each lit for ~3/4 of a ~2 s step. Filmed, it maps pin -> row/colour.
#    define FINDER_STEP_FRAMES 130 // ~2 s at ~15 ms per frame
// Round 2: the Tab and Caps rows' green sinks weren't among the first 18 pins.
// Candidates: P0.0, P1.0-P1.5 (Air60 underglow pins), P7.5-P7.7 (driven by stock).
#    define FINDER_PINS 10
static __code const uint8_t finder_port[FINDER_PINS] = {0, 1, 1, 1, 1, 1, 1, 7, 7, 7};
static __code const uint8_t finder_bit[FINDER_PINS]  = {0, 0, 1, 2, 3, 4, 5, 5, 6, 7};
static uint16_t finder_frames;
static bool     finder_ready;

static bool finder_step(void)
{
    if (!finder_ready) {
        finder_ready = true;
        GPIO_LOW(0, _P0_0);
        GPIO_OUTPUT(0, _P0_0);
        GPIO_LOW(1, 0x3F);
        GPIO_OUTPUT(1, 0x3F);
        GPIO_LOW(7, (uint8_t)(_P7_5 | _P7_6 | _P7_7));
        GPIO_OUTPUT(7, (uint8_t)(_P7_5 | _P7_6 | _P7_7));
    }
    // candidates back off every subframe (pre_update only parks the known sinks)
    GPIO_LOW(0, _P0_0);
    GPIO_LOW(1, 0x3F);
    GPIO_LOW(7, (uint8_t)(_P7_5 | _P7_6 | _P7_7));

    if (++anim_ctr >= (uint8_t)(LED_ROWS * LED_COLS)) {
        anim_ctr = 0;
        finder_frames++;
    }
    const uint16_t step = (finder_frames / FINDER_STEP_FRAMES) % (uint16_t)(FINDER_PINS + 2u);
    const bool     on   = step >= 2u && (finder_frames % FINDER_STEP_FRAMES) < (FINDER_STEP_FRAMES * 3u / 4u);

    indicators_pwm_disable();
    if (on) {
        SET_PWM_DUTY_2(LED_PWM_C0, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C1, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C2, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C3, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C4, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C5, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C6, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C7, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C8, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C9, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C10, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C11, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C12, LED_DUTY(255));
        SET_PWM_DUTY_2(LED_PWM_C13, LED_DUTY(255));
        const uint8_t i = (uint8_t)(step - 2u);
        const uint8_t m = (uint8_t)(1u << finder_bit[i]);
        switch (finder_port[i]) {
            case 0: P0 |= m; break;
            case 1: P1 |= m; break;
            default: P7 |= m; break;
        }
        indicators_pwm_enable();
    }
    return anim_ctr == 0;
}
#endif

bool indicators_update_step(keyboard_state_t *keyboard, uint8_t current_step)
{
    keyboard;
    current_step;
#ifdef GM610_PIN_FINDER
    return finder_step();
#else

    if (++anim_ctr >= (uint8_t)(LED_ROWS * LED_COLS)) {
        anim_ctr  = 0;
        led_phase = (uint8_t)(led_phase + user_settings.led_speed);
        frame_ctr++;
        if (status_frames && --status_frames == 0) {
            status_row = 0xFF;
        }
        if (battery_frames) {
            battery_frames--;
        }
        render_dirty = true;
    }

    indicators_pwm_disable();

    bool lit = led_set_columns(); // load 14 duties while parked

    if (lit) {
        led_enable_sink();       // raise this subframe's sink while parked
        indicators_pwm_enable(); // re-enable last - the selected row lights now
    }

    bool frame_wrapped = false;
    if (++led_color >= 3) {
        led_color = 0;
        if (++led_row >= LED_ROWS) {
            led_row       = 0;
            frame_wrapped = true;
        }
    }

    return frame_wrapped;
#endif
}

void indicators_post_update()
{
    PWM00CON &= ~(1 << 5);
}

// colour scaled by the global brightness
static void scaled(const uint8_t *src, uint8_t *out)
{
    out[0] = SCALE_BRI(src[0]);
    out[1] = SCALE_BRI(src[1]);
    out[2] = SCALE_BRI(src[2]);
}

static void led_regen_one()
{
    uint8_t rgb[3] = {0, 0, 0};

    const uint8_t layer = action_layer < 4 ? action_layer : 0;
    const bool    hl    = layer && ((user_settings.layer_hl >> layer) & 1u);

    const bool bat_bar = battery_valid && (battery_frames || user_settings.battery_indicator_on) && regen_row == BAT_BAR_ROW && regen_col >= BAT_BAR_COL0 &&
                         regen_col < (uint8_t)(BAT_BAR_COL0 + BAT_BAR_LEN);

    if (bat_bar) {
        // keys 1..level lit (at least one); red when low, amber, green when nearly full; the rest dim
        const uint8_t level = keyboard_state.battery_level < 1 ? 1 : keyboard_state.battery_level;
        if ((uint8_t)(regen_col - BAT_BAR_COL0) < level) {
            if (keyboard_state.low_power || level <= 1) {
                rgb[0] = 255;
            } else if (level >= 6) {
                rgb[1] = 255;
            } else {
                rgb[0] = 255;
                rgb[1] = 180;
            }
        } else {
            rgb[0] = rgb[1] = rgb[2] = 24;
        }
    } else if (regen_row == status_row && regen_col == status_col && !(status_blink && (frame_ctr & 0x08))) {
        rgb[0] = status_rgb[0];
        rgb[1] = status_rgb[1];
        rgb[2] = status_rgb[2];
    } else if (!keycolor_is_auto(layer, regen_row, regen_col)) {
        // a colour set for this key on this layer wins over effects and highlights
        scaled(keycolor_rgb(layer, regen_row, regen_col), rgb);
    } else if (hl) {
        // layer highlight: what this layer defines lights in its colour, the rest goes dark
        const uint16_t kc = keymap_get(layer, regen_row, regen_col);
        if (kc != KC_TRANSPARENT && kc != KC_NO) {
            scaled(user_settings.layer_rgb[layer - 1u], rgb);
        }
    } else if (layer && !keycolor_is_auto(0, regen_row, regen_col)) {
        // no highlight on this layer: the base layer's own colour shows through
        scaled(keycolor_rgb(0, regen_row, regen_col), rgb);
    } else if (user_settings.led_effect == FX_SOLID) {
        scaled(user_settings.solid_rgb, rgb); // the core's solid is white; here it's the saved colour
    } else if (user_settings.led_effect < FX_OFF) {
        led_effect_rgb((led_effect_t)user_settings.led_effect, regen_row, regen_col, led_phase, user_settings.led_brightness, rgb);
    }

    led_fb[regen_row][0][regen_col] = rgb[0];
    led_fb[regen_row][1][regen_col] = rgb[1];
    led_fb[regen_row][2][regen_col] = rgb[2];

    if (++regen_col >= LED_COLS) {
        regen_col = 0;
        if (++regen_row >= LED_ROWS) {
            regen_row = 0;
        }
    }
}

static void led_enable_sink()
{
    switch (led_row) {
        case 0:
            if (led_color == 0)
                RGB_R0R = 1;
            else if (led_color == 1)
                RGB_R0G = 1;
            else
                RGB_R0B = 1;
            break;
        case 1:
            if (led_color == 0)
                RGB_R1R = 1;
            else if (led_color == 1)
                RGB_R1G = 1;
            else
                RGB_R1B = 1;
            break;
        case 2:
            if (led_color == 0)
                RGB_R2R = 1;
            else if (led_color == 1)
                RGB_R2G = 1;
            else
                RGB_R2B = 1;
            break;
        case 3:
            if (led_color == 0)
                RGB_R3R = 1;
            else if (led_color == 1)
                RGB_R3G = 1;
            else
                RGB_R3B = 1;
            break;
        case 4:
            if (led_color == 0)
                RGB_R4R = 1;
            else if (led_color == 1)
                RGB_R4G = 1;
            else
                RGB_R4B = 1;
            break;
    }
}

static bool led_set_columns()
{
    __xdata uint8_t *fb = led_fb[led_row][led_color];

    uint8_t any = 0;
    SET_PWM_DUTY_2(LED_PWM_C0, LED_DUTY(fb[0]));
    any = (uint8_t)(any | fb[0]);
    SET_PWM_DUTY_2(LED_PWM_C1, LED_DUTY(fb[1]));
    any = (uint8_t)(any | fb[1]);
    SET_PWM_DUTY_2(LED_PWM_C2, LED_DUTY(fb[2]));
    any = (uint8_t)(any | fb[2]);
    SET_PWM_DUTY_2(LED_PWM_C3, LED_DUTY(fb[3]));
    any = (uint8_t)(any | fb[3]);
    SET_PWM_DUTY_2(LED_PWM_C4, LED_DUTY(fb[4]));
    any = (uint8_t)(any | fb[4]);
    SET_PWM_DUTY_2(LED_PWM_C5, LED_DUTY(fb[5]));
    any = (uint8_t)(any | fb[5]);
    SET_PWM_DUTY_2(LED_PWM_C6, LED_DUTY(fb[6]));
    any = (uint8_t)(any | fb[6]);
    SET_PWM_DUTY_2(LED_PWM_C7, LED_DUTY(fb[7]));
    any = (uint8_t)(any | fb[7]);
    SET_PWM_DUTY_2(LED_PWM_C8, LED_DUTY(fb[8]));
    any = (uint8_t)(any | fb[8]);
    SET_PWM_DUTY_2(LED_PWM_C9, LED_DUTY(fb[9]));
    any = (uint8_t)(any | fb[9]);
    SET_PWM_DUTY_2(LED_PWM_C10, LED_DUTY(fb[10]));
    any = (uint8_t)(any | fb[10]);
    SET_PWM_DUTY_2(LED_PWM_C11, LED_DUTY(fb[11]));
    any = (uint8_t)(any | fb[11]);
    SET_PWM_DUTY_2(LED_PWM_C12, LED_DUTY(fb[12]));
    any = (uint8_t)(any | fb[12]);
    SET_PWM_DUTY_2(LED_PWM_C13, LED_DUTY(fb[13]));
    any = (uint8_t)(any | fb[13]);
    return any != 0;
}

// Channel groups: PWM0x (P3.x, cols 3-8), PWM1x (P2.x, cols 9-13), PWM4x (P5.0-5.2,
// cols 0-2). Each group's channel 0 carries the group's mode enable; PWM10's pin
// (P2.0) is not an LED column here, so it runs without its output (no PWM_SS).
void indicators_pwm_enable()
{
    PWM00CON = (uint8_t)(PWM_MODE_ENABLE | PWM_SS | PWM_CLK_DIV_4);
    PWM01CON = PWM_SS;
    PWM02CON = PWM_SS;
    PWM03CON = PWM_SS;
    PWM04CON = PWM_SS;
    PWM05CON = PWM_SS;

    PWM10CON = (uint8_t)(PWM_MODE_ENABLE | PWM_CLK_DIV_4);
    PWM11CON = PWM_SS;
    PWM12CON = PWM_SS;
    PWM13CON = PWM_SS;
    PWM14CON = PWM_SS;
    PWM15CON = PWM_SS;

    PWM40CON = (uint8_t)(PWM_MODE_ENABLE | PWM_SS | PWM_CLK_DIV_4);
    PWM41CON = PWM_SS;
    PWM42CON = PWM_SS;
}

// The ~5 ms erase runs with interrupts off, so park the scan and columns across it.
void settings_save_pre(void)
{
    tick_pause();
    indicators_pwm_disable();
}

void settings_save_post(void)
{
    indicators_pwm_enable();
    tick_resume();
}

void indicators_pwm_disable()
{
    PWM00CON = PWM_CON_PARKED;
    PWM01CON = PWM_CON_PARKED;
    PWM02CON = PWM_CON_PARKED;
    PWM03CON = PWM_CON_PARKED;
    PWM04CON = PWM_CON_PARKED;
    PWM05CON = PWM_CON_PARKED;
    PWM10CON = PWM_CON_PARKED;
    PWM11CON = PWM_CON_PARKED;
    PWM12CON = PWM_CON_PARKED;
    PWM13CON = PWM_CON_PARKED;
    PWM14CON = PWM_CON_PARKED;
    PWM15CON = PWM_CON_PARKED;
    PWM40CON = PWM_CON_PARKED;
    PWM41CON = PWM_CON_PARKED;
    PWM42CON = PWM_CON_PARKED;
}
