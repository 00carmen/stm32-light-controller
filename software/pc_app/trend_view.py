"""A dependency-free Tk chart; redraws never create telemetry samples."""

import tkinter as tk
from tkinter import ttk
import time


class TrendView(ttk.Frame):
    SERIES = (("Light", "相对光照", "#1764b0"), ("Target", "目标占空比", "#ce7200"),
              ("PWM", "实际占空比", "#16824b"))
    WINDOW = 120
    GAP = 2.0  # A missing report must not become an invented straight transition.

    def __init__(self, parent, history):
        super().__init__(parent)
        self.history = history
        self.columnconfigure(0, weight=1)
        self.rowconfigure(1, weight=1)
        legend = ttk.Frame(self)
        legend.grid(row=0, column=0, sticky="w")
        for _, title, color in self.SERIES:
            ttk.Label(legend, text=f"━ {title}   ", foreground=color).pack(side="left")
        self.canvas = tk.Canvas(self, width=400, height=170, background="#ffffff", highlightthickness=0)
        self.canvas.grid(row=1, column=0, sticky="nsew")
        self.canvas.bind("<Configure>", lambda event: self.redraw())

    def segments(self, key, start, end):
        segments, segment = [], []
        previous = None
        for stamp, values in self.history.points:
            if stamp < start or stamp > end:
                continue
            value = values[key]
            if value is None or (previous is not None and stamp - previous > self.GAP):
                if segment:
                    segments.append(segment)
                segment = []
            if value is not None:
                segment.append((stamp, value))
            previous = stamp
        if segment:
            segments.append(segment)
        return segments

    def redraw(self, now=None):
        canvas = self.canvas
        canvas.delete("all")
        width, height = max(160, canvas.winfo_width()), max(100, canvas.winfo_height())
        left, top, right, bottom = 43, 12, width - 16, height - 28
        for value in (0, 25, 50, 75, 100):
            y = bottom - (bottom - top) * value / 100
            canvas.create_line(left, y, right, y, fill="#e1e7ed")
            canvas.create_text(left - 5, y, text=f"{value}%", anchor="e", fill="#606875")
        origin = self.history.origin
        now = time.monotonic() if now is None else now
        end = max(origin + self.WINDOW, now) if origin is not None else now
        start = end - self.WINDOW
        for fraction in (0, .25, .5, .75, 1):
            x = left + (right - left) * fraction
            elapsed = start + fraction * self.WINDOW - (origin if origin is not None else start)
            canvas.create_text(x, bottom + 15, text=f"{elapsed:.0f}s", fill="#606875")
        if not self.history.points:
            canvas.create_text((left + right) / 2, (top + bottom) / 2,
                               text="等待有效 STATUS · 显示最近 120 秒", fill="#606875")
            return
        for key, _, color in self.SERIES:
            for segment in self.segments(key, start, end):
                coordinates = []
                for stamp, value in segment:
                    coordinates.extend((left + (stamp - start) * (right - left) / self.WINDOW,
                                        bottom - value * (bottom - top) / 100))
                if len(segment) == 1:
                    x, y = coordinates
                    canvas.create_oval(x-2, y-2, x+2, y+2, fill=color, outline=color)
                else:
                    canvas.create_line(*coordinates, fill=color, width=2,
                                       dash=(6, 3) if key == "Target" else ())
