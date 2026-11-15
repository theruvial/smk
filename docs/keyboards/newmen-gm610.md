# Newmen GM610

> **Warning: AI-written firmware, use at your own risk.** The GM610 port and the live editor were vibe coded with
> Claude (Anthropic's AI, through Claude Code). They were tested on one keyboard, and not everything was checked
> (see [Untested and broken](#untested-and-broken)). Flashing firmware to your own hardware can leave it
> unresponsive or permanently bricked. The stock bootloader is never overwritten, so a bad build usually can be undone
> over USB with the [failsafe](#if-smk-doesnt-enumerate), but a build that doesn't start at all needs an ICP programmer
> to recover. **Back up your stock firmware before you flash anything**, and don't flash it if you can't afford to
> lose the keyboard. Nobody here is responsible if it breaks.

Developed and tested on one unit of the [Newmen GM610 wireless mechanical keyboard](https://www.amazon.com/dp/B08QV9D8WN) (PCB `K948 US BYK916 V1.1`).
Everything here was checked on that unit only. See [Untested and broken](#untested-and-broken) before relying on it.

## Specs

- MCU: [SH68F90A](../platforms/sh68f90.md), marked `BYK916` (PCB `K948 US BYK916 V1.1`, LQFP-64)
- Layout: 60% ANSI, 61 keys. The Menu key is at column 12, and there are no arrow keys.
- Matrix: 5 rows x 14 columns (rows P7.1-7.3, P5.3, P5.4; columns P5.0-5.2, P3.5-3.0, P2.5-2.1)
- Backlight: per-key RGB, 5 x 14, common-anode LEDs with transistor sinks (map below)
- Wireless: Beken BK3632 over bit-banged SPI (Bluetooth 1-3; 2.4 GHz is in the code, but there is no dongle to test with)
- Battery sleep: inactivity timer set in the editor, key wake
- USB: `12c9:6001`, the stock ID, so one `sinowisp` command flashes either firmware

## What SMK adds over the stock firmware

Compared with the stock firmware as it shipped on the author's unit:

- **Live keymap editor.** Remap any key on 4 layers from a web page, with no reflashing and no vendor software. Changes
  are saved on the keyboard. Keys can be found by keycode (hex or decimal), name or category.
- **Per-key colours, per layer.** Pick any RGB colour for each key, separately on every layer, with a colour square,
  RGB sliders and a hex box. Keys can follow the effect ("Auto") instead. The solid colour is any RGB too.
- **Layer highlight.** While a layer is held, its keys light in a colour of your choice and keys the layer doesn't use
  go dark, so you can see what you're pressing.
- **Layer stacking.** Holding one momentary layer and pressing another stacks them, and a key resolves from the highest
  layer that defines it. A release acts on the key that was pressed, so a key can't stick when a layer changes under it.
- **Key tester in the editor.** Each press lights up on the page as the firmware sees it, labelled for the layer in use.
- **Configurable sleep timer.** Idle time before Bluetooth sleep is set from the editor, from 1 minute up to 120 on the
  slider (or Never), and applies at once.
- **Failsafe.** Hold Esc, or S+M+K, while plugging the keyboard in to drop into the stock bootloader and reflash, even
  when the firmware is broken. A custom combo can be added in the editor. The stock bootloader has no key combo of its own.
- **Keymap kept across flashes.** [`tools/keep-settings.py`](../../tools/keep-settings.py) carries the saved keymap
  and colours over a reflash, which otherwise wipes them.
- **Connection status on the keys.** Switching mode flashes the active mode on the Tab or Q/W/E/R key, and blinks it
  while pairing. The chosen mode is remembered.
- **Open and testable.** Builds from source with SDCC and meson, and the USB stack and the editor protocol are covered by
  simulator tests.

Not carried over from the stock firmware: its charging indicator. The battery level bar is new but its scale is unverified
(see "Untested and broken"). Its own lighting effects and Fn
combos were not catalogued, so SMK's effects (from the Air60 firmware) and Fn layer are a different set.

## What this fork adds on top of upstream SMK

Upstream SMK ([karstx/smk](https://github.com/karstx/smk)) has a fixed keymap compiled into the firmware. This fork
(branch `newmen-gm610`, about 3,500 added lines) adds, beyond the GM610 board port itself:

**In the core, usable by any board that opts in with `dynamic_keymap_layers`:**

- **Live keymap store.** The keymap lives in flash sectors below the settings sector and is edited at run time.
  It also holds an RGB colour for every key on every layer. The store is versioned, and older versions migrate on load.
- **Configuration protocol.** A vendor HID feature report (ID 8, 7-byte payload, polled over the control endpoint) with
  commands for keycodes, key tester, boot keys, lighting, per-key and layer colours, and the sleep timer. The firmware
  checks every value: key positions, keycode ranges, duplicate boot keys.
- **Editor.** A WebHID page (`docs/editor/`) hosted on GitHub Pages, with a keycode search, palette, failsafe settings, lighting
  manager and key tester, plus a mock keyboard for working on the page without hardware.
- **Lighting manager.** Per-key RGB per layer, a solid-colour effect in any RGB, layer highlight colours, live
  brightness, speed and effect control, and a reset to defaults. Colours go through the same store as the keymap.
- **Layer stacking.** Reworked layer handling in `matrix.c`: stacked momentary layers, per-key resolution from the
  highest active layer, and a cache so a release matches the press. The active layer is reported to the tester.
- **Configurable failsafe.** Up to three extra keys checked at power-up alongside the built-in Esc and S+M+K combos,
  stored in settings and set from the editor.
- **Sleep timer.** An idle-minutes setting, counted in seconds from the board's LED frame rate, with the editor control.
- **Settings.** The saved settings now carry the connection mode, boot keys, solid and layer colours, layer highlight
  flags and the sleep time, within the 28-byte limit of the smallest settings store (the CIY X77's EEPROM).
- **Build.** `dynamic_keymap_layers` and `dynamic_keymap_sectors` options reserve the flash for the store, and
  `led_subframe_us` sets the LED refresh timing per board.
- **Tests.** `tests/test_config.py` (19 tests) runs the protocol in the uCsim simulator: transport, validation, round
  trips, migration from older stores, and the colours and sleep commands.
- **Tools.** `tools/keep-settings.py` carries the saved store over a reflash.

**In the GM610 board port (`src/keyboards/newmen-gm610/`):** a 5 x 14 matrix and per-key RGB driver with the measured
sink map, Bluetooth with a key-selected, saved connection mode, status LEDs on the keys, battery sleep with key wake,
and the failsafe at power-up. The USB interrupt runs at top priority, because the fast LED refresh otherwise starves
enumeration.

## SMK Supported Features

- [x] Key Scan
- [x] Per-key RGB (effects, brightness, speed, solid colour, per-key colours per layer, layer highlight)
- [x] Bluetooth (BK3632), connection mode saved
- [x] Settings persistence
- [x] Live keymap editor (4 layers, WebHID): <https://theruvial.github.io/smk/editor/>
- [x] Battery sleep (Bluetooth mode, wakes on a key)
- [x] Battery level bar on keys 1-7 (Fn+[ show, Fn+] keep on, Fn+\ off; scale unverified, see below)
- [x] Failsafe: hold Esc, or S+M+K, while plugging in (a custom combo can be set in the editor)
- [ ] 2.4 GHz (untested, no dongle)

## Untested and broken

Verified on the author's unit: typing on all 61 keys, the Fn layer, per-key RGB and the editor (Chrome, Windows),
Bluetooth pairing and typing with one Windows laptop, the failsafe, and Bluetooth sleep with key wake.

**Broken on the author's unit (hardware, not firmware):**

- The green channel of the Tab and Caps rows (P6.2 and P6.3, chip pins 3 and 4) lights nothing, most likely a lifted or
  bridged pin from probing. Those two rows can't make green, so their colours are off. Other units are likely fine.

**Not implemented:**

- Charging indicator. No charge-status bit or pin has been identified.
- The sixth LED row the stock firmware drives (P4.0, P4.1, P6.0). No LEDs hang off it on this board.

**Untested:**

- 2.4 GHz (`Fn`+`R`). The code is there, but there is no dongle to test it with.
- Bluetooth beyond one Windows laptop: the other two slots, other operating systems, phones, and reconnecting after long sleeps.
- Wired sleep, where the keyboard sleeps when the PC suspends the USB bus, and waking the PC with a key.
- How the wake works. The sleep sequence copies the stock firmware's, and a key wakes it. Which pin INT4 listens on was
  never confirmed.
- Battery level scale. The level comes from the Bluetooth chip's status byte (low 3 bits, 0-7) and the bar uses the Air60's
  cut-offs (red 1 or below, green 6 or above). On the tested unit a full battery (100% per Windows) lit 5 keys, so the real
  scale or a flag in the other status bits is not understood. Treat the bar as a rough guide. The stock firmware reads the
  same status bytes and does not use the MCU's ADC. Whether the level is also reported to the host is up to the chip:
  Windows showed a battery percentage for the paired keyboard.
- Battery life. Nothing here measured the current drawn asleep or awake.
- The editor outside Chrome on Windows. It uses WebHID, so it needs a Chromium browser (Chrome or Edge); Firefox and Safari don't have it.
- Other units. The stock firmware revision can differ, the code options were never read (they need an ICP programmer),
  and an ISO or other variant of the board wasn't looked at.
- The Air60 and the other boards still build and pass the simulator suites, but none of them were flashed after the
  keymap, protocol, layer-stacking, or settings changes in this branch.

## Fn layer

Hold Fn:

- `Fn`+`Tab` wired USB; `Fn`+`Q`/`W`/`E` Bluetooth slot 1/2/3 (tap to select, hold about 3 s to pair); `Fn`+`R` 2.4 GHz
- `Fn`+`V`/`B` brightness down/up, `Fn`+`N`/`M` previous/next effect, `Fn`+`,`/`.` speed down/up
- The number row gives F1-F12 under `Fn` (plus Delete and a backtick key). The full map is in `layouts/default/layout.c`, and every layer can be changed live in the editor.

## LED sink map

Every colour row is a sink pin pulled low. Mapped by lighting one pin at a time and filming it.

| Row | R | G | B |
|---|---|---|---|
| 0 (Esc) | P0.4 | P6.1 | P0.3 |
| 1 (Tab) | P0.2 | P6.2 | P6.6 |
| 2 (Caps) | P6.7 | P6.3 | P5.7 |
| 3 (Shift) | P4.5 | P6.4 | P4.6 |
| 4 (Ctrl) | P4.3 | P6.5 | P4.4 |

P4.0, P4.1 and P6.0 drive nothing. The stock firmware drives them as an unused sixth row.
On the author's unit the Tab and Caps rows have no green: P6.2 and P6.3 (chip pins 3 and 4, next to GND pin 2) light
nothing, most likely a lifted or bridged pin. That is a fault on that unit, not something the firmware does.

## Flashing

Everything runs from Windows with [sinowisp](https://github.com/karstx/sinowisp/releases) (v2.1.0 used here;
SHA-256 of the Windows archive `63180e0d35fdd4e68834723fe2abec796e03cfdd1a168dc7745da83672ef71ab`).
Power switch on, cable in, stock firmware in wired mode (hold `Fn`+`Tab` for 3 s).

1. **Back up the stock firmware first.** Do not skip this, and do not publish the file (it is Newmen's firmware):

   ```
   .\sinowisp.exe read --platform sh68f90 --vendor_id 0x12c9 --product_id 0x6001 --isp_iface_num 1 --isp_report_id 5 --section full --format bin gm610-stock-full.bin
   ```

   It should end with `Successfully read 65536 bytes`. Keep two copies somewhere private.

2. Flash SMK (`--force` is needed; sinowisp pads the image to the 61,440-byte app area):

   ```
   .\sinowisp.exe write --platform sh68f90 --vendor_id 0x12c9 --product_id 0x6001 --isp_iface_num 1 --isp_report_id 5 --format ihex --force --reboot true newmen-gm610_default_smk.hex
   ```

3. The keyboard comes back as "SMK Keyboard" (`12c9:6001`). Open the editor in Chrome or Edge and connect.

To go back to stock, write the first 61,440 bytes of your backup (`--format bin`, the same flags). The bootloader
(`0xF000`-`0xFFFF`) is never touched, which is what makes the way back safe.

Reflashing wipes the saved keymap and colours, which live just below the settings sector (`0xE600`-`0xEDFF`).
[`tools/keep-settings.py`](../../tools/keep-settings.py) builds an image that keeps that region from a fresh read of the keyboard.

### If SMK doesn't enumerate

Unplug, power switch off then on, **hold Esc (or S+M+K) and plug in the cable**. The keyboard shows up as
`0603:1020 "Gaming KB"`, the stock bootloader, and the same `sinowisp write` command works. This needs the SMK app to boot;
a build that crashes earlier needs an ICP programmer.

## Known-good stock dump (for checking yours)

One unit, read 2026-11-13 with sinowisp 2.1.0. Stock USB ID `12c9:6001`, manufacturer "SINO WEALTH",
product "Newmen Bluetooth Keyboard". Firmware revisions can differ between units, so a different hash is not by itself a problem.

| File | Size | MD5 | SHA-256 |
|---|---|---|---|
| Full read (app + bootloader) | 65,536 | `3aadaf3c7f74a4092b60c0cb3e20bea2` | `eb349ac573f1f5667bf137bec4433b9198ed70bd2b0c1727c654ecc8e832fd69` |
| App area only (first 61,440 bytes) | 61,440 | `f536098ec11aaea34e22f322fe063217` | `863c7da05cb1b1493fe577e2d6be63569b976e7f0541706fb95733c04165d741` |

The stock bootloader is the last 4,096 bytes of the full read. Code options cannot be read over USB ISP (they need an ICP programmer).

## Notes for porting

- Pins and matrix were read from the stock firmware's port set-up and checked with a multimeter. Probing the chip's pin feet can bend them
  onto their neighbours (it happened once: column 0 shorted to GND). Probe the switch pads and test pads instead.
- Sleep wake: the stock firmware parks every key column and LED sink low, leaves the rows as pull-ups, arms INT4
  (`IENC=0xF3`) and enters power-down. SMK does the same. A wired board only sleeps when the PC suspends the USB bus.
