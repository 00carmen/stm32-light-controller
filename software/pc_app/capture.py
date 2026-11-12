"""Validated telemetry history and explicit, bounded-lifetime CSV recording."""

from collections import deque
import csv
from datetime import datetime
from pathlib import Path
import time


def number(value):
    """Keep a sensor error missing instead of drawing/recording a false zero."""
    return None if value in (None, "ERR") else int(value.rstrip("%"))


class TelemetryHistory:
    def __init__(self, max_points=1200):
        self.points = deque(maxlen=max_points)
        self.origin = None

    def clear(self):
        self.points.clear()
        self.origin = None

    def append(self, fields, now):
        if self.origin is None:
            self.origin = now
        self.points.append((now, {key: number(fields.get(key)) for key in ("Light", "Target", "PWM")}))


class CSVRecorder:
    STATUS_COLUMNS = (
        ("mode", "Mode"), ("adc_raw", "ADC"), ("adc_avg", "AVG"),
        ("voltage_mV", "mV"), ("light_percent", "Light"),
        ("target_percent", "Target"), ("pwm_percent", "PWM"),
        ("pot_raw", "Pot"), ("knob_percent", "Knob"),
        ("remote_percent", "Remote"), ("rx_errors", "RxErr"),
    )
    CONFIG_COLUMNS = (("cal_dark", "Dark"), ("cal_bright", "Bright"), ("ramp_ms", "RampMs"))
    HEADER = ("time_iso", "elapsed_s") + tuple(name for name, _ in STATUS_COLUMNS + CONFIG_COLUMNS)

    def __init__(self):
        self.file = None
        self.writer = None
        self.path = None
        self.rows = 0
        self.started = None
        self.last_flush = None

    @property
    def active(self):
        return self.file is not None

    def start(self, path, now=None):
        if self.active:
            raise ValueError("已有 CSV 正在记录")
        handle = Path(path).open("w", newline="", encoding="utf-8-sig")
        try:
            writer = csv.writer(handle)
            writer.writerow(self.HEADER)
            handle.flush()
        except Exception:
            handle.close()
            raise
        self.file, self.writer, self.path = handle, writer, Path(path)
        self.rows = 0
        self.started = time.monotonic() if now is None else now
        self.last_flush = self.started

    def write(self, fields, config=None, now=None, wall_time=None):
        if not self.active:
            return
        now = time.monotonic() if now is None else now
        stamp = datetime.now().astimezone() if wall_time is None else wall_time
        row = [stamp.isoformat(timespec="milliseconds"), f"{now - self.started:.3f}"]
        for _, key in self.STATUS_COLUMNS:
            value = fields.get(key)
            row.append("" if value in (None, "ERR") else value.rstrip("%"))
        row.extend((config or {}).get(key, "") for _, key in self.CONFIG_COLUMNS)
        self.writer.writerow(row)
        self.rows += 1
        if now - self.last_flush >= 1:
            self.file.flush()
            self.last_flush = now

    def stop(self):
        if self.file is None:
            return
        handle, self.file, self.writer = self.file, None, None
        handle.close()
