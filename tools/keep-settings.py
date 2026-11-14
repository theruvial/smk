"""Build a flash image that keeps the keyboard's saved keymap and settings.

sinowisp writes the whole 61,440-byte app area, so a plain reflash wipes the
live-keymap store and the settings sector. This takes a fresh read of the
keyboard (sinowisp read --section firmware) and the new firmware .hex, and
writes a .bin with the new code plus the old store region.

usage: python keep-settings.py <old-app.bin> <new.hex> <out.bin> <store_start_hex> <store_end_hex> [<move_from_hex> <move_len_hex>]

With the optional pair, the old keymap store at <move_from> (<move_len> bytes) is
copied to <store_start> instead: for a firmware whose store grew and moved down.
"""
import sys

old_path, hex_path, out_path = sys.argv[1:4]
start, end = int(sys.argv[4], 16), int(sys.argv[5], 16)
move = (int(sys.argv[6], 16), int(sys.argv[7], 16)) if len(sys.argv) > 7 else None
APP = 61440

old = open(old_path, "rb").read()
assert len(old) == APP, f"expected a {APP}-byte app read, got {len(old)}"

img = bytearray(APP)  # sinowisp pads with zeros too
upper = 0
for line in open(hex_path):
    line = line.strip()
    if not line.startswith(":"):
        continue
    n, addr, kind = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
    data = bytes.fromhex(line[9:9 + 2 * n])
    if kind == 0x04:
        upper = int.from_bytes(data, "big") << 16
    elif kind == 0x00:
        a = upper + addr
        assert a + n <= start or a >= end, f"code overlaps the store at 0x{a:04x}"
        img[a:a + n] = data

img[start:end] = old[start:end]
if move:
    src, n = move
    img[start:start + n] = old[src:src + n]
    print(f"moved the keymap store 0x{src:04x}-0x{src + n - 1:04x} to 0x{start:04x}")
open(out_path, "wb").write(img)
print(f"kept 0x{start:04x}-0x{end - 1:04x} ({end - start} bytes) from the keyboard")
