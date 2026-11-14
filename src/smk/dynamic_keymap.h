#pragma once

// Runtime-editable keymap. With DYNAMIC_KEYMAP_ENABLE the keymap lives in XRAM,
// seeded from the compiled keymaps[] and persisted to its own flash store, so a
// host tool can remap keys without a rebuild. Without it, keymap_get() reads the
// compiled table directly and nothing else here exists.
//
// A board enabling it must define exactly DYNAMIC_KEYMAP_LAYERS layers in its
// keymaps[] (the store is sized from that, not from the layout).

#include <stdint.h>
#include <stdbool.h>
#include "kbdef.h"
#include "layout.h"

#ifdef DYNAMIC_KEYMAP_ENABLE

void     dynamic_keymap_init(void);
uint16_t keymap_get(uint8_t layer, uint8_t row, uint8_t col);
void     keymap_set(uint8_t layer, uint8_t row, uint8_t col, uint16_t keycode);
void     dynamic_keymap_reset(void);        // keymap back to the compiled keymaps[], saved
void     dynamic_keymap_reset_colors(void); // every key colour back to auto, saved
void     dynamic_keymap_save(void);         // no-op when flash already matches

// Per-key colour, per layer, stored with the keymap: an RGB value, or "auto"
// (base layer: follow the effect; other layers: the layer highlight). Black = off.
bool                   keycolor_is_auto(uint8_t layer, uint8_t row, uint8_t col);
const __xdata uint8_t *keycolor_rgb(uint8_t layer, uint8_t row, uint8_t col); // 3 bytes; only if !auto
void                   keycolor_set_rgb(uint8_t layer, uint8_t row, uint8_t col, uint8_t r, uint8_t g, uint8_t b);
void                   keycolor_set_auto(uint8_t layer, uint8_t row, uint8_t col);

#else

#    define keymap_get(layer, row, col) (keymaps[(layer)][(row)][(col)])

#endif
