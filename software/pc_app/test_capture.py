import csv
from datetime import datetime, timezone, timedelta
from pathlib import Path
from tempfile import TemporaryDirectory
import time
import tkinter as tk
import unittest
from unittest.mock import patch

from app import ControlApp
from capture import CSVRecorder, TelemetryHistory
from protocol import parse_line
from test_console import STATUS, CONFIG, DummyTransport
from transport import TransportEvent
from trend_view import TrendView


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.path = Path(self.temp.name) / "light.csv"
        self.recorder = CSVRecorder()
        self.fields = parse_line(STATUS).fields

    def tearDown(self):
        self.recorder.stop()
        self.temp.cleanup()

    def read_rows(self):
        with self.path.open(encoding="utf-8-sig", newline="") as handle:
            return list(csv.DictReader(handle))

    def test_csv_units_errors_metadata_and_time(self):
        self.recorder.start(self.path, now=10)
        fields = dict(self.fields, Light="ERR", Knob="ERR", ADC="ERR")
        stamp = datetime(2026, 11, 12, 15, 30, tzinfo=timezone(timedelta(hours=8)))
        self.recorder.write(fields, now=10.5, wall_time=stamp)
        self.recorder.write(self.fields, parse_line(CONFIG).fields, now=11, wall_time=stamp)
        self.recorder.stop()
        self.assertTrue(self.path.read_bytes().startswith(b"\xef\xbb\xbf"))
        rows = self.read_rows()
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["elapsed_s"], "0.500")
        self.assertTrue(rows[0]["time_iso"].endswith("+08:00"))
        for key in ("light_percent", "knob_percent", "adc_raw", "cal_dark"):
            self.assertEqual(rows[0][key], "")
        self.assertEqual(rows[1]["light_percent"], "53")
        self.assertEqual(rows[1]["cal_dark"], "3764")
        self.assertEqual(rows[1]["ramp_ms"], "40")

    def test_stop_is_idempotent_and_file_released(self):
        self.recorder.start(self.path)
        self.recorder.write(self.fields)
        self.recorder.stop()
        self.recorder.stop()
        self.path.rename(self.path.with_suffix(".done"))
        self.assertFalse(self.recorder.active)

    def test_start_failure_and_duplicate_start(self):
        with self.assertRaises(OSError):
            self.recorder.start(self.path / "missing.csv")
        self.assertFalse(self.recorder.active)
        self.recorder.start(self.path)
        with self.assertRaises(ValueError):
            self.recorder.start(self.path)
        self.recorder.write(self.fields)
        self.recorder.stop()
        self.assertEqual(len(self.read_rows()), 1)

    def test_history_bounded_and_keeps_missing_values(self):
        history = TelemetryHistory(max_points=3)
        for index in range(10):
            history.append(dict(self.fields, Light="ERR" if index == 9 else "53%"), index)
        self.assertEqual(len(history.points), 3)
        self.assertEqual(history.origin, 0)
        self.assertIsNone(history.points[-1][1]["Light"])
        history.clear()
        self.assertEqual(len(history.points), 0)
        self.assertIsNone(history.origin)

    def test_chart_splits_missing_and_stale_reports(self):
        history = TelemetryHistory()
        for stamp, light in ((0, "53%"), (.5, "54%"), (1, "ERR"), (1.5, "55%"), (5, "56%")):
            history.append(dict(self.fields, Light=light), stamp)
        # Exercise the production segment logic without opening an extra window.
        chart = object.__new__(TrendView)
        chart.history = history
        self.assertEqual([len(s) for s in chart.segments("Light", 0, 10)], [2, 1, 1])
        self.assertEqual([len(s) for s in chart.segments("PWM", 0, 10)], [4, 1])


class CaptureInterfaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.path = Path(self.temp.name) / "record.csv"
        self.root = tk.Tk()
        self.root.withdraw()
        self.app = ControlApp(self.root, ports_provider=lambda: [("COM7", "CH340")])
        self.app.port_open = True
        self.app.transport = DummyTransport()
        self.app.handle_line(CONFIG)

    def tearDown(self):
        self.app.close()
        self.temp.cleanup()

    def rows(self):
        with self.path.open(encoding="utf-8-sig", newline="") as handle:
            return list(csv.DictReader(handle))

    def test_only_valid_new_status_is_recorded(self):
        start = time.monotonic()
        self.app.recorder.start(self.path, now=start)
        self.app.handle_line(STATUS, received_at=start - 1)  # Queued before Start.
        self.app.handle_line("OK STREAM ON")
        self.app.handle_line(STATUS.replace("PWM=35%", "PWM=150%"))
        self.app.handle_line(STATUS, received_at=start + .5)
        self.app.trend.redraw(start + .7)
        self.app.trend.redraw(start + 1)
        self.app.stop_recording()
        self.assertEqual(len(self.rows()), 1)
        self.assertEqual(self.rows()[0]["elapsed_s"], "0.500")

    def test_clear_curve_does_not_clear_csv(self):
        self.app.recorder.start(self.path)
        self.app.handle_line(STATUS)
        self.app.clear_history()
        self.app.handle_line(STATUS)
        self.app.stop_recording()
        self.assertEqual(len(self.rows()), 2)
        self.assertEqual(len(self.app.history.points), 1)

    def test_parameters_blank_until_reconfirmed(self):
        self.app.recorder.start(self.path)
        self.app.handle_line(STATUS)
        self.app.handle_line("OK RAMP 100")
        self.app.handle_line(STATUS)
        self.app.handle_line(CONFIG.replace("RampMs=40", "RampMs=100"))
        self.app.handle_line(STATUS)
        self.app.stop_recording()
        self.assertEqual([r["ramp_ms"] for r in self.rows()], ["40", "", "100"])

    def test_disconnect_flushes_csv(self):
        self.app.recorder.start(self.path)
        self.app.handle_line(STATUS)
        self.app.events.put(TransportEvent(self.app.session_id, "disconnected"))
        self.app.poll_events()
        self.assertFalse(self.app.recorder.active)
        self.assertEqual(len(self.rows()), 1)
        self.assertTrue(self.app.record_start_button.instate(["disabled"]))

    def test_start_cancel_and_then_record(self):
        with patch("app.filedialog.asksaveasfilename", return_value=""):
            self.app.start_recording()
        self.assertFalse(self.app.recorder.active)
        with patch("app.filedialog.asksaveasfilename", return_value=str(self.path)):
            self.app.start_recording()
        self.assertTrue(self.app.recorder.active)
        self.assertEqual(self.app.transport.commands, ["STREAM ON"])
        self.app.handle_line(STATUS)
        self.app.stop_recording()
        self.assertEqual(len(self.rows()), 1)

    def test_write_failure_stops_recording_without_crashing(self):
        self.app.recorder.start(self.path)
        with patch.object(self.app.recorder, "write", side_effect=OSError("disk full")):
            self.app.handle_line(STATUS)
        self.assertFalse(self.app.recorder.active)
        self.assertIn("CSV 写入失败", self.app.action_text.get())


if __name__ == "__main__":
    unittest.main(verbosity=2)
