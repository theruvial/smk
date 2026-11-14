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
OK, ERR_CMD, ERR_RANGE, ERR_KEYCODE, ERR_NO_KEY = 0, 1, 2, 3, 4
QK_USER = 0x7E40


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

    def __init__(self):
        self.u = UcsimSession(SIM.firmware)
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
        s = Session().command(SET_KEYCODE, 4, 0, 0, 0, QK_USER & 0xFF, QK_USER >> 8).get().finish()
        self.assertEqual(s.responses()[-1][:3], [SET_KEYCODE, 4, ERR_KEYCODE], s.output)

    def test_unknown_command_rejected(self):
        s = Session().command(0x7F, 5).get().finish()
        self.assertEqual(s.responses()[-1][:3], [0x7F, 5, ERR_CMD], s.output)

    def test_get_matrix_reports_no_keys_pressed(self):
        s = Session().command(GET_MATRIX, 6, 0).get().finish()
        self.assertEqual(s.responses()[-1][:5], [GET_MATRIX, 6, OK, 0, 0], s.output)


if __name__ == "__main__":
    unittest.main()
