# Newmen GM610

## Specs

- MCU: [SH68F90A](../platforms/sh68f90.md), marked `BYK916` (PCB `K948 US BYK916 V1.1`, LQFP-64)
- Layout: 60% ANSI, 61 keys. The Menu key is at column 12, and there are no arrow keys.
- Matrix: 5 rows x 14 columns (rows P7.1-7.3, P5.3, P5.4; columns P5.0-5.2, P3.5-3.0, P2.5-2.1)
- Backlight: per-key RGB, 5 x 14, common-anode LEDs with transistor sinks (map below)
- Wireless: Beken BK3632 over bit-banged SPI (Bluetooth 1-3; 2.4 GHz is in the code, but there is no dongle to test with)
- Battery sleep: inactivity timer set in the editor, key wake
- USB: `12c9:6001`, the stock ID, so one `sinowisp` command flashes either firmware

## SMK Supported Features

- [x] Key Scan
- [x] Per-key RGB (effects, brightness, speed, solid colour, per-key colours per layer, layer highlight)
- [x] Bluetooth (BK3632), connection mode saved
- [x] Settings persistence
- [x] Live keymap editor (4 layers, WebHID): <https://theruvial.github.io/smk/editor/>
- [x] Battery sleep (Bluetooth mode, wakes on a key)
- [x] Failsafe: hold Esc, or S+M+K, while plugging in (a custom combo can be set in the editor)
- [ ] 2.4 GHz (untested, no dongle)

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
