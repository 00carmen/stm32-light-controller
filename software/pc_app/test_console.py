import queue
import threading
import time
import tkinter as tk
import unittest

import serial
from app import ControlApp
from protocol import (LineFramer, calibration_command, matches_reply, parse_line,
                      pwm_command, ramp_command, wire_command)
from transport import SerialSession

STATUS = "STATUS,Mode=REMOTE,ADC=2000,AVG=2010,mV=1620,Light=53%,Target=40%,PWM=35%,Pot=2048,Knob=50%,Remote=40%,RxErr=0"
CONFIG = "CONFIG,Dark=3764,Bright=481,RampMs=40,Storage=FLASH,Saved=1,Dirty=0,Seq=2"


class ProtocolTests(unittest.TestCase):
    def test_one_newline(self):
        self.assertEqual(wire_command(" GET\r\n"), b"GET\r\n")
        for value in ("GET\nSAVE", "GET\x00", "GET\x7f", "中文", "", "X" * 64):
            with self.subTest(value=value), self.assertRaises(ValueError):
                wire_command(value)
        self.assertEqual(len(wire_command("X" * 63)), 65)

    def test_input_validation(self):
        self.assertEqual(calibration_command("3764", "481"), "CAL 3764 481")
        for dark, bright in (("1000", "2000"), ("1000", "950"), ("5000", "0")):
            with self.assertRaises(ValueError):
                calibration_command(dark, bright)
        for value in ("0", "201", "20ms", "-1"):
            with self.assertRaises(ValueError):
                ramp_command(value)
        self.assertEqual(pwm_command("100"), "PWM 100")
        with self.assertRaises(ValueError):
            pwm_command("150")

    def test_device_status(self):
        self.assertEqual(parse_line(STATUS).fields["PWM"], "35%")
        damaged = [STATUS.replace("Mode=REMOTE", "Mode=OTHER"),
                   STATUS.replace("PWM=35%", "PWM=150%"),
                   STATUS.replace("PWM=35%", "PWM=35"),
                   STATUS + ",PWM=20%", "STATUS,Mode=AUTO"]
        for line in damaged:
            with self.subTest(line=line), self.assertRaises(ValueError):
                parse_line(line)
        failure = STATUS.replace("ADC=2000,AVG=2010,mV=1620,Light=53%", "ADC=ERR,AVG=ERR,mV=ERR,Light=ERR")
        self.assertEqual(parse_line(failure).fields["ADC"], "ERR")

    def test_config(self):
        self.assertEqual(parse_line(CONFIG).fields["Seq"], "2")
        for line in (CONFIG.replace("RampMs=40", "RampMs=0"),
                     CONFIG.replace("Bright=481", "Bright=3764"),
                     CONFIG.replace("Saved=1", "Saved=2")):
            with self.assertRaises(ValueError):
                parse_line(line)

    def test_fragmented_lines(self):
        framer = LineFramer()
        self.assertEqual(framer.feed(b"OK PW"), [])
        self.assertEqual(framer.feed(b"M 20\r"), ["OK PWM 20"])
        self.assertEqual(framer.feed(b"\nOK SAVE\nCONFIG,x=1\r\n"), ["OK SAVE", "CONFIG,x=1"])

    def test_bad_frames_recover(self):
        framer = LineFramer(limit=4)
        self.assertEqual(framer.feed(b"12345\nOK\n"), ["HOST_ERR LINE_TOO_LONG", "OK"])
        self.assertEqual(framer.feed(b"X\xffY\rGET\r"), ["HOST_ERR NON_ASCII_LINE", "GET"])

    def test_matching_replies(self):
        self.assertTrue(matches_reply("PWM 020", "OK PWM 20"))
        self.assertFalse(matches_reply("PWM 80", "OK PWM 20"))
        self.assertFalse(matches_reply("MODE REMOTE", "OK MODE AUTO"))
        self.assertTrue(matches_reply("SAVE", "OK SAVE UNCHANGED"))
        self.assertTrue(matches_reply("GET", STATUS))
        self.assertFalse(matches_reply("CONFIG", STATUS))
        self.assertTrue(matches_reply("PWM 150", "ERR RANGE_0_100"))


class FakePort:
    def __init__(self, replies=None, fail_open=False):
        self.is_open = False
        self.fail_open = fail_open
        self.buffer = bytearray()
        self.lock = threading.Lock()
        self.writes = []
        self.replies = replies or {}

    def open(self):
        if self.fail_open:
            raise serial.SerialException("port busy")
        self.is_open = True

    def reset_input_buffer(self):
        with self.lock:
            self.buffer.clear()

    def close(self):
        self.is_open = False

    @property
    def in_waiting(self):
        with self.lock:
            return len(self.buffer)

    def write(self, packet):
        with self.lock:
            self.writes.append(packet)
            self.buffer.extend(self.replies.get(packet, b""))
        return len(packet)

    def read(self, count):
        with self.lock:
            chunk = bytes(self.buffer[:min(count, 7)])
            del self.buffer[:len(chunk)]
        if not chunk:
            time.sleep(0.005)
        return chunk


class TransportTests(unittest.TestCase):
    def test_batch_queue_full_is_atomic(self):
        worker = SerialSession("FAKE", queue.Queue())
        for _ in range(31):
            worker.send("GET")
        with self.assertRaises(ValueError):
            worker.send_many(["CAL 3764 481", "RAMP 40", "CONFIG"])
        self.assertEqual(worker.commands.qsize(), 31)

    def run_session(self, port, commands, expected_kind, expected_count=1):
        events = queue.Queue()
        worker = SerialSession("FAKE", events, serial_factory=lambda: port, reply_timeout=0.15)
        for command in commands:
            worker.send(command)
        worker.start()
        found = []
        until = time.monotonic() + 2
        try:
            while time.monotonic() < until:
                try:
                    event = events.get(timeout=0.05)
                except queue.Empty:
                    continue
                found.append(event)
                if sum(e.kind == expected_kind for e in found) >= expected_count:
                    break
            self.assertGreaterEqual(sum(e.kind == expected_kind for e in found), expected_count)
        finally:
            worker.stop()
            worker.join(timeout=1)
        self.assertFalse(worker.is_alive())
        self.assertFalse(port.is_open)
        return found

    def test_acknowledged_order(self):
        port = FakePort({b"GET\r\n": (STATUS + "\r\n").encode(),
                         b"CONFIG\r\n": (CONFIG + "\r\n").encode(),
                         b"SAVE\r\n": b"OK SAVE UNCHANGED\r\n"})
        self.run_session(port, ["GET", "CONFIG", "SAVE"], "ack", 3)
        self.assertEqual(port.writes, [b"GET\r\n", b"CONFIG\r\n", b"SAVE\r\n"])

    def test_rejected_batch_stops(self):
        port = FakePort({b"PWM 150\r\n": b"ERR RANGE_0_100\r\n"})
        self.run_session(port, ["PWM 150", "SAVE"], "rejected")
        self.assertEqual(port.writes, [b"PWM 150\r\n"])

    def test_timeout_does_not_send_next(self):
        port = FakePort()
        self.run_session(port, ["GET", "SAVE"], "timeout")
        self.assertEqual(port.writes, [b"GET\r\n"])

    def test_open_failure_releases(self):
        port = FakePort(fail_open=True)
        events = self.run_session(port, [], "error")
        self.assertIn("port busy", next(e.text for e in events if e.kind == "error"))


class DummyTransport:
    def __init__(self):
        self.commands = []

    def send(self, command):
        self.commands.append(command)

    def send_many(self, commands):
        self.commands.extend(commands)

    def stop(self):
        pass

    def join(self, timeout=None):
        pass


class InterfaceTests(unittest.TestCase):
    def setUp(self):
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = ControlApp(self.root, ports_provider=lambda: [("COM7", "CH340")])

    def tearDown(self):
        self.app.close()

    def test_initial_and_connected_state(self):
        self.assertFalse(self.app.port_open)
        self.assertTrue(self.app.remote_buttons[-1].instate(["disabled"]))
        self.app.port_open = True
        self.app.transport = DummyTransport()
        self.app.handle_line(STATUS)
        self.assertTrue(self.app.remote_buttons[-1].instate(["!disabled"]))
        self.app.handle_line("EVENT MODE=AUTO")
        self.assertTrue(self.app.remote_buttons[-1].instate(["disabled"]))

    def test_parameter_edit_does_not_save_implicitly(self):
        self.app.port_open = True
        self.app.transport = DummyTransport()
        self.app.handle_line(CONFIG)
        self.assertTrue(self.app.save_button.instate(["!disabled"]))
        self.app.mark_edited()
        self.assertFalse(self.app.config_edited)
        self.app.ramp.set("100")
        self.app.mark_edited()
        self.assertTrue(self.app.save_button.instate(["disabled"]))
        self.app.apply_config()
        self.assertEqual(self.app.transport.commands, ["CAL 3764 481", "RAMP 100", "CONFIG"])
        self.assertNotIn("SAVE", self.app.transport.commands)

    def test_invalid_edit_sends_nothing(self):
        self.app.port_open = True
        self.app.transport = DummyTransport()
        self.app.dark.set("1000")
        self.app.bright.set("2000")
        self.app.ramp.set("40")
        self.app.apply_config()
        self.assertEqual(self.app.transport.commands, [])

    def test_widget_layout_budget(self):
        self.root.update_idletasks()
        width = self.app.frame.winfo_reqwidth()
        height = self.app.frame.winfo_reqheight()
        self.assertLessEqual(width, 1100, (width, height))
        self.assertLessEqual(height, 720, (width, height))
        for index in range(len(self.app.tabs.tabs())):
            self.app.tabs.select(index)
            self.root.update_idletasks()
            self.assertLessEqual(self.app.frame.winfo_reqheight(), 720)


if __name__ == "__main__":
    unittest.main(verbosity=2)
