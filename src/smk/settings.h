#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t led_effect;
    uint8_t led_brightness;
    uint8_t led_speed;
    uint8_t led_color;
    uint8_t ul_effect;
    uint8_t ul_brightness;
    uint8_t ul_speed;
    uint8_t battery_indicator_on;
    uint8_t rf_link;
    uint8_t conn_mode; // boards without a USB/RF slider: key-selected mode (1 = USB, 0 = RF)
    uint8_t boot_keys[3]; // extra power-up bootloader keys, (row << 4) | col each, 0xFF = unused
    uint8_t solid_rgb[3];    // colour of the solid effect
    uint8_t layer_rgb[3][3]; // highlight colour of layers 1..3 while held (index = layer - 1)
    uint8_t layer_hl;        // bit n: layer n highlights its keys (off: the base layer shows through)
} user_settings_t;

#define BOOT_KEY_NONE 0xFFu

extern user_settings_t user_settings;

bool settings_load(void);

void settings_save(void);

void settings_mark_dirty(void);

void settings_task(void);

void settings_save_pre(void);
void settings_save_post(void);

#if DEBUG == 1
void settings_dump(void);
#endif
