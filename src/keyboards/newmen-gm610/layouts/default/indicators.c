#include "indicators.h"
#include "kbdef.h"
#include "gpio.h"
#include "pwm.h"
#include "settings.h"
#include "tick.h"
#include "keyboard.h"
#include "led_effect.h"
#include "user_led.h"
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

// Solid effect colour, as a position on the colour wheel (led_color_wheel): 0 = red,
// 85 = green, 170 = blue. 48 is purple (r=111, g=0, b=144).
#define LED_COLOR_DEFAULT 48
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

void        indicators_pwm_enable();
void        indicators_pwm_disable();
static void led_regen_one();
static void led_enable_sink();
static bool led_set_columns();

void indicators_apply_defaults()
{
    user_settings.led_effect     = FX_SOLID;
    user_settings.led_color      = LED_COLOR_DEFAULT;
    user_settings.led_brightness = LED_BRIGHTNESS_DEFAULT;
    user_settings.led_speed      = LED_SPEED_DEFAULT;
    user_settings.ul_effect      = FX_OFF;
    user_settings.ul_brightness  = 0;
    user_settings.ul_speed       = LED_SPEED_MIN;
    user_settings.battery_indicator_on = 0;
    user_settings.rf_link        = 0; // RF_MODE_2_4G
    user_settings.conn_mode      = 1; // USB
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

bool indicators_update_step(keyboard_state_t *keyboard, uint8_t current_step)
{
    keyboard;
    current_step;

    if (++anim_ctr >= (uint8_t)(LED_ROWS * LED_COLS)) {
        anim_ctr  = 0;
        led_phase = (uint8_t)(led_phase + user_settings.led_speed);
        frame_ctr++;
        if (status_frames && --status_frames == 0) {
            status_row = 0xFF;
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
}

void indicators_post_update()
{
    PWM00CON &= ~(1 << 5);
}

static void led_regen_one()
{
    uint8_t rgb[3] = {0, 0, 0};

    if (regen_row == status_row && regen_col == status_col && !(status_blink && (frame_ctr & 0x08))) {
        rgb[0] = status_rgb[0];
        rgb[1] = status_rgb[1];
        rgb[2] = status_rgb[2];
    } else if (user_settings.led_effect == FX_SOLID) {
        // the core's solid is white; here it's the saved colour
        led_color_wheel(user_settings.led_color, rgb);
        rgb[0] = SCALE_BRI(rgb[0]);
        rgb[1] = SCALE_BRI(rgb[1]);
        rgb[2] = SCALE_BRI(rgb[2]);
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
