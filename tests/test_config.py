"""Keymap configuration protocol (REPORT_ID_CONFIG feature report, src/smk/config_protocol.c).

Needs a firmware built with `dynamic_keymap_layers` (e.g. newmen-gm610); other
images skip. Point SMK_FIRMWARE at it:
    SMK_FIRMWARE=$PWD/newmen-gm610_default_smk.hex python3 -m unittest ../tests/test_config.py
"""

import unittest
from pathlib import Path

from devices import UcsimSession
from sim import Sim, load_symbols

SIM = Sim()

REPORT_ID_CONFIG = 0x08
PAYLOAD = 7
EP0CON = 0x97
OEP0RDY, OEP0STL, IEP0STL = 0x01, 0x02, 0x08
SETUPIF, OW = 0x10, 0x20
OEP0IF = 0x10
STUB = 0x9100  # LCALL _config_task ; LJMP SLED_END

GET_INFO, GET_KEYCODES, SET_KEYCODE, GET_MATRIX = 0x01, 0x02, 0x03, 0x05
GET_BOOT_KEYS, SET_BOOT_KEYS = 0x07, 0x08
OK, ERR_CMD, ERR_RANGE, ERR_KEYCODE, ERR_NO_KEY = 0, 1, 2, 3, 4
UNKNOWN_CUSTOM = 0x7F00  # past QK_USER and past any board's custom range


def setUpModule():
    reason = SIM.available()
    if reason:
        raise unittest.SkipTest(reason)
    syms = load_symbols(Path(SIM.firmware).with_suffix(".map"))
    if "config_task" not in syms:
        raise unittest.SkipTest("firmware built without dynamic_keymap_layers")
    global CONFIG_TASK
    CONFIG_TASK = syms["config_task"]


def set_report_config(length=1 + PAYLOAD):
    return [0x21, 0x09, REPORT_ID_CONFIG, 0x03, 0x01, 0x00, length & 0xFF, 0x00]


def get_report_config(length=1 + PAYLOAD):
    return [0xA1, 0x01, REPORT_ID_CONFIG, 0x03, 0x01, 0x00, length & 0xFF, 0x00]


class Session:
    """EP0 driver (same model as test_usb_ep0.Session) plus a way to run the main-loop task."""

    def __init__(self, rom_patch=None):
        self.u = UcsimSession(SIM.firmware)
        for addr, data in (rom_patch or {}).items():  # planted before boot, e.g. an old keymap store
            for off in range(0, len(data), 48):
                chunk = data[off:off + 48]
                self.u.cmd(f"set mem rom 0x{addr + off:x} " + " ".join(f"0x{b:02x}" for b in chunk))
        for c in SIM._boot_to_post_init():
            self.u.cmd(c)
        self.u.brk(SIM.SLED_END)
        self.u.cmd(f"set mem rom 0x{STUB:x} 0x12 0x{CONFIG_TASK >> 8:02x} 0x{CONFIG_TASK & 0xFF:02x} "
                   f"0x02 0x{SIM.SLED_END >> 8:02x} 0x{SIM.SLED_END & 0xFF:02x}")

    def _irq(self, usbif1=0, usbif2=0, out=None):
        if out is not None:
            self.u.cmd(SIM._set_xram(SIM.EP0_OUT_BUF, list(out) + [0] * (8 - len(out))))
        if usbif2:
            self.u.set_sfr(SIM.USBIF2, usbif2)
        if usbif1:
            self.u.set_sfr(SIM.USBIF1, usbif1)
        self.u.cmd("pc 0x9000")
        self.u.run()
        return self

    def setup(self, pkt):
        ep0con = self.u.get_sfr(EP0CON)
        self.u.cmd(SIM._set_xram(SIM.EP0_OUT_BUF, pkt))
        extra = 0 if ep0con & OEP0RDY else OW
        self.u.set_sfr(EP0CON, ep0con & ~(OEP0RDY | OEP0STL | IEP0STL))
        return self._irq(usbif1=SETUPIF | extra)

    def out(self, data=()):
        ep0con = self.u.get_sfr(EP0CON)
        self.u.set_sfr(EP0CON, ep0con & ~OEP0RDY)
        self.u.set_sfr(SIM.OEP0CNT, len(data))
        return self._irq(usbif2=OEP0IF, out=data)

    def task(self):
        self.u.cmd(f"pc 0x{STUB:x}")
        self.u.run()
        return self

    def command(self, *payload):
        data = [REPORT_ID_CONFIG] + list(payload) + [0] * (PAYLOAD - len(payload))
        return self.setup(set_report_config()).out(data).task()

    def get(self):
        return self.setup(get_report_config()).out()

    def finish(self, *sfrs):
        self.regs = {a: self.u.get_sfr(a) for a in sfrs}
        self.u.close()
        self.output = self.u.stderr_text()
        self.packets = SIM.in_packets(self.output)
        return self

    def sfr(self, addr):
        return self.regs[addr]

    def responses(self):
        """The 8-byte GET_REPORT packets, report ID stripped."""
        return [p[1:] for p in self.packets if len(p) == 1 + PAYLOAD]


class TestConfigTransport(unittest.TestCase):
    def test_set_report_acks_status(self):
        s = Session().setup(set_report_config()).out([REPORT_ID_CONFIG, GET_INFO, 1, 0, 0, 0, 0, 0]).finish()
        self.assertEqual(s.packets, [[]], s.output)

    def test_wrong_length_set_report_stalls(self):
        s = Session().setup(set_report_config(length=6)).finish(EP0CON)
        self.assertEqual(s.packets, [], s.output)
        self.assertEqual(s.sfr(EP0CON) & (IEP0STL | OEP0STL), IEP0STL | OEP0STL, s.output)

    def test_get_report_returns_report_id_and_payload(self):
        s = Session().get().finish()
        self.assertEqual([len(p) for p in s.packets], [1 + PAYLOAD], s.output)
        self.assertEqual(s.packets[0][0], REPORT_ID_CONFIG, s.output)


class TestConfigCommands(unittest.TestCase):
    def test_get_info(self):
        s = Session().command(GET_INFO, 0x11).get().finish()
        cmd, seq, status, ver, rows, cols, layers = s.responses()[-1]
        self.assertEqual((cmd, seq, status, ver), (GET_INFO, 0x11, OK, 1), s.output)
        self.assertGreater(rows, 0)
        self.assertGreater(cols, 0)
        self.assertGreater(layers, 0)

    def test_set_then_get_keycode_round_trip(self):
        # row 0 col 0 is a real key on every board (Esc on the GM610)
        s = (Session()
             .command(SET_KEYCODE, 1, 0, 0, 0, 0x14, 0x00).get()      # layer 0 (0,0) := KC_Q
             .command(GET_KEYCODES, 2, 0, 0, 0).get()
             .finish())
        set_resp, get_resp = s.responses()[-2:]
        self.assertEqual(set_resp[:3], [SET_KEYCODE, 1, OK], s.output)
        self.assertEqual(get_resp[:3], [GET_KEYCODES, 2, OK], s.output)
        self.assertEqual(get_resp[3] | get_resp[4] << 8, 0x0014, s.output)

    def test_out_of_range_rejected(self):
        s = Session().command(SET_KEYCODE, 3, 99, 0, 0, 0x14, 0).get().finish()
        self.assertEqual(s.responses()[-1][:3], [SET_KEYCODE, 3, ERR_RANGE], s.output)

    def test_custom_keycode_rejected(self):
        s = Session().command(SET_KEYCODE, 4, 0, 0, 0, UNKNOWN_CUSTOM & 0xFF, UNKNOWN_CUSTOM >> 8).get().finish()
        self.assertEqual(s.responses()[-1][:3], [SET_KEYCODE, 4, ERR_KEYCODE], s.output)

    def test_boot_keys_round_trip(self):
        # (0,1) and (1,1): "1" and "Q" on the GM610; third slot unused
        s = (Session()
             .command(SET_BOOT_KEYS, 7, 0x01, 0x11, 0xFF).get()
             .command(GET_BOOT_KEYS, 8).get()
             .finish())
        set_resp, get_resp = s.responses()[-2:]
        self.assertEqual(set_resp[:3], [SET_BOOT_KEYS, 7, OK], s.output)
        self.assertEqual(get_resp[:6], [GET_BOOT_KEYS, 8, OK, 0x01, 0x11, 0xFF], s.output)

    def test_lighting_round_trip(self):
        # effect 1, brightness 200, speed 5 are in range for every LED board
        s = (Session()
             .command(0x0A, 11, 1, 200, 5).get()
             .command(0x09, 12).get()
             .finish())
        set_resp, get_resp = s.responses()[-2:]
        self.assertEqual(set_resp[:3], [0x0A, 11, OK], s.output)
        self.assertEqual(get_resp[:6], [0x09, 12, OK, 1, 200, 5], s.output)

    def test_key_rgb_round_trip(self):
        # layer 1, (0,0) := rgb(10, 20, 30); then it reads back non-auto. (0,1) stays auto.
        s = (Session()
             .command(0x0E, 13, (1 << 4) | 0, 0, 10, 20, 30).get()
             .command(0x0D, 14, 1, 0, 0).get()
             .command(0x0D, 15, 1, 0, 1).get()
             .finish())
        set_resp, get0, get1 = s.responses()[-3:]
        self.assertEqual(set_resp[:3], [0x0E, 13, OK], s.output)
        self.assertEqual(get0[:7], [0x0D, 14, OK, 10, 20, 30, 0], s.output)
        self.assertEqual(get1[6], 1, s.output)  # auto

    def test_key_back_to_auto(self):
        s = (Session()
             .command(0x0E, 16, (2 << 4) | 1, 1, 255, 0, 0).get()
             .command(0x10, 17, 2, 1, 1).get()
             .command(0x0D, 18, 2, 1, 1).get()
             .finish())
        self.assertEqual(s.responses()[-1][6], 1, s.output)

    def test_v2_store_migrates_to_rgb(self):
        # a v2 store (keymap + one colour index per key) at FLASH_KEYMAP_ADDR, as a
        # keyboard flashed before the RGB change has it; it must load and convert
        syms = load_symbols(Path(SIM.firmware).with_suffix(".map"))
        rows, cols, layers = 5, 14, 4
        keys = rows * cols * layers
        keymap = bytes(keys * 2)                      # all KC_NO: contents don't matter here
        idx = bytearray([0xFF] * keys)                # auto everywhere...
        idx[(1 * rows + 0) * cols + 0] = 85           # layer 1 (0,0): wheel 85 = pure blue
        idx[(1 * rows + 0) * cols + 1] = 0xFE         # layer 1 (0,1): off
        idx[(1 * rows + 0) * cols + 2] = 0xFD         # layer 1 (0,2): white
        payload = keymap + bytes(idx)
        s16 = sum(payload) & 0xFFFF
        store = bytes([ord("D"), ord("K"), 2, rows, cols, layers]) + payload + bytes([s16 & 0xFF, s16 >> 8])
        addr = syms.get("FLASH_KEYMAP_ADDR") or 0xE600
        s = (Session({addr: store})
             .command(0x0D, 30, 1, 0, 0).get()
             .command(0x0D, 31, 1, 0, 1).get()
             .command(0x0D, 32, 1, 0, 2).get()
             .command(0x0D, 33, 1, 0, 3).get()
             .finish())
        blue, off, white, auto = s.responses()[-4:]
        self.assertEqual(blue[3:7], [0, 0, 255, 0], s.output)
        self.assertEqual(off[3:7], [0, 0, 0, 0], s.output)
        self.assertEqual(white[3:7], [255, 255, 255, 0], s.output)
        self.assertEqual(auto[6], 1, s.output)

    def test_solid_and_layer_rgb(self):
        s = (Session()
             .command(0x12, 19, 1, 2, 3).get()
             .command(0x11, 20).get()
             .command(0x0C, 21, 2, 1, 7, 8, 9).get()
             .command(0x0B, 22, 2).get()
             .finish())
        solid, layer = s.responses()[-3], s.responses()[-1]
        self.assertEqual(solid[:6], [0x11, 20, OK, 1, 2, 3], s.output)
        self.assertEqual(layer[:7], [0x0B, 22, OK, 1, 7, 8, 9], s.output)

    def test_sleep_timer_round_trip(self):
        # 0 = never, 240 = the maximum; 241 is rejected and leaves the value alone
        s = (Session()
             .command(0x14, 24, 17).get()
             .command(0x13, 25).get()
             .command(0x14, 26, 241).get()
             .command(0x13, 27).get()
             .finish())
        r = s.responses()
        self.assertEqual(r[-4][:3], [0x14, 24, OK], s.output)
        self.assertEqual(r[-3][:4], [0x13, 25, OK, 17], s.output)
        self.assertEqual(r[-2][:3], [0x14, 26, ERR_RANGE], s.output)
        self.assertEqual(r[-1][:4], [0x13, 27, OK, 17], s.output)

    def test_base_layer_has_no_highlight(self):
        s = Session().command(0x0B, 23, 0).get().finish()
        self.assertEqual(s.responses()[-1][:3], [0x0B, 23, ERR_RANGE], s.output)

    def test_boot_keys_duplicates_rejected(self):
        s = Session().command(SET_BOOT_KEYS, 9, 0x01, 0x01, 0xFF).get().finish()
        self.assertEqual(s.responses()[-1][:3], [SET_BOOT_KEYS, 9, ERR_RANGE], s.output)

    def test_boot_keys_out_of_range_rejected(self):
        s = Session().command(SET_BOOT_KEYS, 10, 0x9F, 0xFF, 0xFF).get().finish()
        self.assertEqual(s.responses()[-1][:3], [SET_BOOT_KEYS, 10, ERR_RANGE], s.output)

    def test_unknown_command_rejected(self):
        s = Session().command(0x7F, 5).get().finish()
        self.assertEqual(s.responses()[-1][:3], [0x7F, 5, ERR_CMD], s.output)

    def test_get_matrix_reports_no_keys_pressed(self):
        s = Session().command(GET_MATRIX, 6, 0).get().finish()
        self.assertEqual(s.responses()[-1][:5], [GET_MATRIX, 6, OK, 0, 0], s.output)


if __name__ == "__main__":
    unittest.main()
