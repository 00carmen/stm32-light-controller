"""光照项目控制台 V0.2：设备控制、实时曲线与 CSV 记录。"""

from datetime import datetime
import logging
from pathlib import Path
from queue import Queue, Empty
import time
import tkinter as tk
from tkinter import ttk, filedialog
from tkinter.scrolledtext import ScrolledText

from protocol import calibration_command, parse_line, pwm_command, ramp_command
from transport import SerialSession, available_ports
from capture import TelemetryHistory, CSVRecorder
from trend_view import TrendView


class ControlApp:
    def __init__(self, root, session_factory=SerialSession, ports_provider=available_ports):
        self.root = root
        self.session_factory = session_factory
        self.ports_provider = ports_provider
        self.events = Queue()
        self.transport = None
        self.session_id = 0
        self.port_open = False
        self.connecting = False
        self.device_ready = False
        self.device_mode = None
        self.last_status_time = None
        self.latest_config = None
        self.config_values_confirmed = False
        self.history = TelemetryHistory()
        self.recorder = CSVRecorder()
        self.last_chart_draw = 0
        self.config_edited = False
        self.closed = False
        self.log_lines = 0
        self.control_buttons = []
        self.remote_buttons = []
        self.port = tk.StringVar()
        self.connection_text = tk.StringVar(value="未连接")
        self.action_text = tk.StringVar(value="请先关闭旧串口助手的 COM7，再连接开发板。")
        self.data_time_text = tk.StringVar(value="尚未收到设备状态")
        self.config_text = tk.StringVar(value="尚未读取参数")
        self.record_text = tk.StringVar(value="未记录 · CSV 仅保存开始后收到的有效 STATUS")
        self.edit_text = tk.StringVar(value="参数编辑后先应用到 RAM，再按需保存。")
        self.stream = tk.BooleanVar(value=True)
        self.show_status_log = tk.BooleanVar(value=False)
        self.desired_pwm = tk.DoubleVar(value=50)
        self.desired_text = tk.StringVar(value="待发送 50%")
        self.dark = tk.StringVar()
        self.bright = tk.StringVar()
        self.ramp = tk.StringVar()
        self.raw_command = tk.StringVar()
        self.values = {}
        self.value_labels = []
        self._build()
        self.refresh_ports()
        self._update_controls()
        self.poll_job = self.root.after(50, self.poll_events)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.report_callback_exception = self.callback_error

    def _build(self):
        self.root.title("STM32 光照控制台 V0.2")
        self.root.minsize(900, 640)
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        self.root.geometry(f"{max(900, min(1100, sw - 100))}x{max(640, min(740, sh - 100))}")
        style = ttk.Style(self.root)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("TLabel", font=("Microsoft YaHei UI", 10))
        style.configure("TButton", font=("Microsoft YaHei UI", 10), padding=(8, 4))
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 18, "bold"))
        style.configure("Muted.TLabel", foreground="#606875", font=("Microsoft YaHei UI", 9))
        style.configure("Number.TLabel", font=("Consolas", 19, "bold"), foreground="#155a9c")
        frame = ttk.Frame(self.root, padding=14)
        frame.grid(sticky="nsew")
        self.root.rowconfigure(0, weight=1)
        self.root.columnconfigure(0, weight=1)
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(3, weight=1)
        self.frame = frame

        title = ttk.Frame(frame)
        title.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        ttk.Label(title, text="STM32 光照控制台", style="Title.TLabel").pack(side="left")
        ttk.Label(title, text="V0.2  ·  适配固件 V1.8", style="Muted.TLabel").pack(side="right")

        connection = ttk.LabelFrame(frame, text="串口连接", padding=8)
        connection.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        ttk.Label(connection, text="端口").grid(row=0, column=0, padx=(0, 6))
        self.port_box = ttk.Combobox(connection, textvariable=self.port, state="readonly", width=12)
        self.port_box.grid(row=0, column=1)
        self.refresh_button = ttk.Button(connection, text="刷新", command=self.refresh_ports)
        self.refresh_button.grid(row=0, column=2, padx=6)
        self.connect_button = ttk.Button(connection, text="连接", command=self.toggle_connection)
        self.connect_button.grid(row=0, column=3)
        ttk.Label(connection, text="9600 / 8N1", style="Muted.TLabel").grid(row=0, column=4, padx=12)
        connection.columnconfigure(5, weight=1)
        ttk.Label(connection, textvariable=self.connection_text).grid(row=0, column=5, sticky="e")

        data = ttk.LabelFrame(frame, text="设备状态 · 由开发板回复更新", padding=8)
        data.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        metrics = [("Mode", "实际模式"), ("Light", "相对光照"), ("Target", "目标占空比"), ("PWM", "实际占空比"),
                   ("Knob", "旋钮位置"), ("ADC", "光敏原始值"), ("AVG", "光敏滤波值"), ("mV", "估算电压 mV")]
        for index, (key, label) in enumerate(metrics):
            cell = ttk.Frame(data)
            cell.grid(row=index // 4, column=index % 4, sticky="ew", padx=10, pady=3)
            data.columnconfigure(index % 4, weight=1)
            ttk.Label(cell, text=label, style="Muted.TLabel").pack(anchor="w")
            self.values[key] = tk.StringVar(value="—")
            value_label = ttk.Label(cell, textvariable=self.values[key], style="Number.TLabel")
            value_label.pack(anchor="w")
            self.value_labels.append(value_label)
        ttk.Label(data, textvariable=self.data_time_text, style="Muted.TLabel").grid(row=2, column=0, columnspan=4, sticky="w", padx=10)

        self.tabs = ttk.Notebook(frame)
        self.tabs.grid(row=3, column=0, sticky="nsew", pady=(0, 8))
        control_tab = ttk.Frame(self.tabs, padding=8)
        config_tab = ttk.Frame(self.tabs, padding=8)
        log_tab = ttk.Frame(self.tabs, padding=8)
        trend_tab = ttk.Frame(self.tabs, padding=8)
        for tab, title in ((control_tab, "设备控制"), (trend_tab, "实时曲线"),
                           (config_tab, "参数与保存"), (log_tab, "通信记录")):
            tab.columnconfigure(0, weight=1)
            tab.rowconfigure(0, weight=1)
            self.tabs.add(tab, text=title)
        trend_tab.rowconfigure(0, weight=0)
        trend_tab.rowconfigure(1, weight=1)
        toolbar = ttk.Frame(trend_tab)
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 4))
        self.record_start_button = ttk.Button(toolbar, text="开始 CSV 记录", command=self.start_recording)
        self.record_start_button.pack(side="left", padx=(0, 6))
        self.record_stop_button = ttk.Button(toolbar, text="停止并保存", command=self.stop_recording)
        self.record_stop_button.pack(side="left", padx=(0, 6))
        ttk.Button(toolbar, text="清空曲线", command=self.clear_history).pack(side="left")
        self.trend = TrendView(trend_tab, self.history)
        self.trend.grid(row=1, column=0, sticky="nsew")
        ttk.Label(trend_tab, textvariable=self.record_text, style="Muted.TLabel",
                  wraplength=820).grid(row=2, column=0, sticky="w", pady=(3, 0))
        control = ttk.LabelFrame(control_tab, text="模式与远程亮度", padding=8)
        control.grid(row=0, column=0, sticky="nsew")
        for index, (mode, label) in enumerate((("AUTO", "自动 AUTO"), ("LOCAL", "旋钮 LOCAL"), ("REMOTE", "电脑 REMOTE"))):
            button = ttk.Button(control, text=label, command=lambda m=mode: self.send(f"MODE {m}"))
            button.grid(row=0, column=index, padx=(0, 6), sticky="ew")
            self.control_buttons.append(button)
        self.get_button = ttk.Button(control, text="查询状态", command=lambda: self.send("GET"))
        self.get_button.grid(row=0, column=3, padx=6)
        self.stream_button = ttk.Checkbutton(control, text="周期上报", variable=self.stream, command=self.toggle_stream)
        self.stream_button.grid(row=0, column=4, padx=6)
        control.columnconfigure(2, weight=1)
        ttk.Label(control, textvariable=self.desired_text).grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.pwm_scale = ttk.Scale(control, from_=0, to=100, variable=self.desired_pwm, command=self.pwm_changed)
        self.pwm_scale.grid(row=1, column=1, columnspan=3, sticky="ew", padx=6, pady=(8, 0))
        pwm_button = ttk.Button(control, text="发送亮度", command=self.send_pwm)
        pwm_button.grid(row=1, column=4, pady=(8, 0))
        self.remote_buttons.extend([self.pwm_scale, pwm_button])
        ttk.Label(control, text="切到 REMOTE 后拖动滑块，再点发送。实体按键可切回 AUTO。", style="Muted.TLabel").grid(row=2, column=0, columnspan=5, sticky="w", pady=(5, 0))

        config_frame = ttk.LabelFrame(config_tab, text="参数配置 · 应用到 RAM，保存到 Flash", padding=8)
        config_frame.grid(row=0, column=0, sticky="nsew")
        for index, (label, variable) in enumerate((("暗参考", self.dark), ("亮参考", self.bright), ("渐变 ms/百分点", self.ramp))):
            ttk.Label(config_frame, text=label).grid(row=0, column=index * 2, sticky="w", padx=(0, 6))
            entry = ttk.Entry(config_frame, textvariable=variable, width=9)
            entry.grid(row=0, column=index * 2 + 1, sticky="ew", padx=(0, 16))
            entry.bind("<KeyRelease>", self.mark_edited)
            config_frame.columnconfigure(index * 2 + 1, weight=1)
        actions = ttk.Frame(config_frame)
        actions.grid(row=1, column=0, columnspan=6, sticky="w", pady=(8, 4))
        self.read_config_button = ttk.Button(actions, text="读取参数", command=self.read_config)
        self.read_config_button.pack(side="left", padx=(0, 6))
        apply_button = ttk.Button(actions, text="应用参数（RAM）", command=self.apply_config)
        apply_button.pack(side="left", padx=(0, 6))
        self.save_button = ttk.Button(actions, text="保存到 Flash", command=self.save_config)
        self.save_button.pack(side="left", padx=(0, 6))
        self.load_button = ttk.Button(actions, text="载入已保存", command=self.load_config)
        self.load_button.pack(side="left", padx=(0, 6))
        defaults_button = ttk.Button(actions, text="恢复默认（RAM）", command=self.defaults_config)
        defaults_button.pack(side="left")
        self.control_buttons.extend([apply_button, defaults_button])
        ttk.Label(config_frame, textvariable=self.config_text).grid(row=2, column=0, columnspan=6, sticky="w")
        ttk.Label(config_frame, textvariable=self.edit_text, style="Muted.TLabel").grid(row=3, column=0, columnspan=6, sticky="w")

        log_frame = ttk.LabelFrame(log_tab, text="通信记录", padding=6)
        log_frame.grid(row=0, column=0, sticky="nsew")
        log_frame.rowconfigure(1, weight=1)
        log_frame.columnconfigure(0, weight=1)
        ttk.Checkbutton(log_frame, text="记录周期 STATUS 行", variable=self.show_status_log).grid(row=0, column=0, sticky="w")
        ttk.Button(log_frame, text="清空窗口记录", command=self.clear_log).grid(row=0, column=1, sticky="e")
        self.log = ScrolledText(log_frame, height=5, font=("Consolas", 10), wrap="word", state="disabled")
        self.log.grid(row=1, column=0, columnspan=2, sticky="nsew", pady=(4, 0))
        self.log.tag_configure("error", foreground="#ad1a1a")
        self.log.tag_configure("send", foreground="#155a9c")

        raw = ttk.Frame(frame)
        raw.grid(row=4, column=0, sticky="ew", pady=(0, 6))
        raw.columnconfigure(1, weight=1)
        ttk.Label(raw, text="文本命令").grid(row=0, column=0, padx=(0, 6))
        entry = ttk.Entry(raw, textvariable=self.raw_command)
        entry.grid(row=0, column=1, sticky="ew")
        entry.bind("<Return>", lambda event: self.send_raw())
        self.raw_button = ttk.Button(raw, text="发送（自动换行）", command=self.send_raw)
        self.raw_button.grid(row=0, column=2, padx=(6, 0))
        ttk.Label(frame, textvariable=self.action_text, style="Muted.TLabel", wraplength=850).grid(row=5, column=0, sticky="w")

    @staticmethod
    def _enabled(widget, enabled):
        widget.configure(state="normal" if enabled else "disabled")

    def _update_controls(self):
        active = self.port_open and self.device_ready
        self.port_box.configure(state="disabled" if self.port_open or self.connecting else "readonly")
        self._enabled(self.refresh_button, not self.port_open and not self.connecting)
        self._enabled(self.connect_button, not self.connecting)
        self.connect_button.configure(text="断开" if self.port_open else "连接")
        for widget in self.control_buttons:
            self._enabled(widget, active)
        for widget in self.remote_buttons:
            self._enabled(widget, active and self.device_mode == "REMOTE")
        for widget in (self.get_button, self.read_config_button, self.raw_button, self.stream_button):
            self._enabled(widget, self.port_open)
        flash = active and self.latest_config and self.latest_config.get("Storage") == "FLASH"
        self._enabled(self.save_button, bool(flash and not self.config_edited))
        self._enabled(self.load_button, bool(flash and self.latest_config.get("Saved") == "1"))
        self._enabled(self.record_start_button, active and not self.recorder.active)
        self._enabled(self.record_stop_button, self.recorder.active)

    def start_recording(self):
        if not self.port_open or not self.device_ready or self.recorder.active:
            return
        directory = Path(__file__).with_name("records")
        try:
            directory.mkdir(exist_ok=True)
            path = filedialog.asksaveasfilename(
                parent=self.root, title="保存光照测试 CSV", initialdir=str(directory),
                initialfile=datetime.now().strftime("light_%Y%m%d_%H%M%S.csv"),
                defaultextension=".csv", filetypes=[("CSV 数据", "*.csv")])
            if not path:
                return
            if not self.port_open or not self.device_ready:
                self.show_error("设备已断开或无回复，请恢复连接后再开始记录。")
                return
            self.recorder.start(path)
        except (OSError, ValueError) as exc:
            self.show_error(f"无法开始 CSV 记录：{exc}")
            return
        self.update_record_text()
        self._update_controls()
        self.send("STREAM ON")

    def update_record_text(self):
        if self.recorder.path:
            state = "记录中" if self.recorder.active else "已停止"
            self.record_text.set(f"{state} · {self.recorder.rows} 条 · {self.recorder.path}")

    def stop_recording(self):
        try:
            self.recorder.stop()
        except OSError as exc:
            self.show_error(f"CSV 关闭失败，请检查磁盘；部分末尾数据可能未保存：{exc}")
        self.update_record_text()
        self._update_controls()

    def clear_history(self):
        self.history.clear()
        self.trend.redraw()

    def refresh_ports(self):
        try:
            ports = self.ports_provider()
            choices = [device for device, description in ports]
            self.port_box.configure(values=choices)
            if self.port.get() not in choices:
                preferred = next((device for device, description in ports if "CH340" in description.upper()), None)
                self.port.set(preferred or (choices[0] if choices else ""))
            if not choices:
                self.action_text.set("未找到串口，请连接 USB 转串口模块后刷新。")
        except Exception as exc:
            self.show_error(f"枚举串口失败：{exc}")

    def toggle_connection(self):
        if self.port_open:
            self.port_open = False
            self.device_ready = False
            self.connecting = True
            self.transport.stop()
            self.connection_text.set("正在释放串口…")
            self._update_controls()
            return
        if self.transport and self.transport.is_alive():
            return
        if not self.port.get():
            self.show_error("请先选择串口")
            return
        self.session_id += 1
        self.connecting = True
        self.device_mode = None
        self.device_ready = False
        self.latest_config = None
        self.last_status_time = None
        self.connection_text.set("正在打开串口…")
        self._update_controls()
        self.transport = self.session_factory(self.port.get(), self.events, session=self.session_id)
        self.transport.start()

    def send(self, command):
        if not self.port_open or self.transport is None:
            self.show_error("串口尚未连接")
            return False
        try:
            self.transport.send(command)
            return True
        except ValueError as exc:
            self.show_error(str(exc))
            return False

    def send_raw(self):
        self.send(self.raw_command.get())

    def send_batch(self, commands):
        if not self.port_open or self.transport is None:
            self.show_error("串口尚未连接")
            return False
        try:
            self.transport.send_many(commands)
            return True
        except ValueError as exc:
            self.show_error(str(exc))
            return False

    def pwm_changed(self, value):
        self.desired_text.set(f"待发送 {round(float(value))}%")

    def send_pwm(self):
        if self.device_mode != "REMOTE":
            self.show_error("请先切换到 REMOTE 模式")
            return
        self.send(pwm_command(str(round(self.desired_pwm.get()))))

    def toggle_stream(self):
        self.send("STREAM ON" if self.stream.get() else "STREAM OFF")

    def mark_edited(self, event=None):
        self.config_edited = self.latest_config is None or any(
            variable.get() != self.latest_config[key]
            for variable, key in ((self.dark, "Dark"), (self.bright, "Bright"), (self.ramp, "RampMs"))
        )
        self.edit_text.set("编辑区有修改：先应用参数到 RAM，再保存到 Flash。" if self.config_edited
                           else "编辑区与设备参数一致。")
        self._update_controls()

    def read_config(self):
        self.config_edited = False
        self.send("CONFIG")

    def apply_config(self):
        try:
            cal = calibration_command(self.dark.get(), self.bright.get())
            ramp = ramp_command(self.ramp.get())
        except ValueError as exc:
            self.show_error(str(exc))
            return
        if self.send_batch((cal, ramp, "CONFIG")):
            self.config_edited = False
            self.edit_text.set("等待设备确认；应用参数不会自动保存到 Flash。")
        self._update_controls()

    def save_config(self):
        if self.config_edited:
            self.show_error("编辑值尚未应用，请先应用或重新读取参数")
            return
        self.send("SAVE")

    def load_config(self):
        self.config_edited = False
        self.send("LOAD")

    def defaults_config(self):
        self.config_edited = False
        self.send("DEFAULTS")

    def clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self.log_lines = 0

    def append_log(self, direction, text, tag=""):
        self.log.configure(state="normal")
        self.log.insert("end", f"{datetime.now():%H:%M:%S} [{direction}] {text}\n", tag)
        self.log_lines += 1
        if self.log_lines > 500:
            self.log.delete("1.0", "101.0")
            self.log_lines -= 100
        self.log.see("end")
        self.log.configure(state="disabled")

    def show_error(self, text):
        self.action_text.set(text)
        self.append_log("提示", text, "error")

    def handle_line(self, text, received_at=None, wall_time=None):
        try:
            message = parse_line(text)
        except ValueError as exc:
            self.show_error(f"收到无效数据：{exc}；原文：{text[:160]}")
            return
        if message.kind != "status" or self.show_status_log.get():
            self.append_log("收", text, "error" if message.kind == "error" else "")
        if message.kind in {"status", "config", "ok", "mode"}:
            self.device_ready = True
            self.connection_text.set(f"{self.port.get()} · 设备已回复")
        if message.kind == "status":
            self.device_mode = message.fields["Mode"]
            for name, variable in self.values.items():
                variable.set(message.fields[name])
            self.last_status_time = time.monotonic() if received_at is None else received_at
            self.history.append(message.fields, self.last_status_time)
            if self.recorder.active and self.last_status_time >= self.recorder.started:
                try:
                    config = self.latest_config if self.config_values_confirmed else None
                    self.recorder.write(message.fields, config, now=self.last_status_time, wall_time=wall_time)
                except OSError as exc:
                    self.stop_recording()
                    self.show_error(f"CSV 写入失败，记录已停止，请检查保存路径或磁盘空间：{exc}")
                self.update_record_text()
            self.last_status_clock = (wall_time or datetime.now()).strftime("%H:%M:%S")
            self.rx_errors = message.fields["RxErr"]
            for label in self.value_labels:
                label.configure(foreground="#155a9c")
        elif message.kind == "mode":
            self.device_mode = message.fields["Mode"]
            self.values["Mode"].set(self.device_mode)
        elif message.kind == "ok":
            if text.startswith(("OK CAL ", "OK RAMP ")) or text in {"OK DEFAULTS", "OK LOAD"}:
                self.config_values_confirmed = False
            if text.startswith("OK MODE "):
                self.device_mode = text.split()[-1]
                self.values["Mode"].set(self.device_mode)
            elif text == "OK STREAM ON":
                self.stream.set(True)
            elif text == "OK STREAM OFF":
                self.stream.set(False)
        elif message.kind == "config":
            self.latest_config = message.fields
            self.config_values_confirmed = True
            if not self.config_edited:
                self.dark.set(message.fields["Dark"])
                self.bright.set(message.fields["Bright"])
                self.ramp.set(message.fields["RampMs"])
                self.edit_text.set("参数已读取；修改后需先应用，SAVE 才会持久化。")
            saved = "有已保存记录" if message.fields["Saved"] == "1" else "尚未保存过"
            dirty = "RAM 有未保存参数" if message.fields["Dirty"] == "1" else "与已保存参数一致"
            self.config_text.set(f"{saved} · {dirty} · 序号 {message.fields['Seq']} · {message.fields['Storage']}")
        elif message.kind == "error":
            self.action_text.set(text)
            if text.startswith("ERR MODE_USE_REMOTE"):
                self.device_mode = None
                self.values["Mode"].set("待查询")
        self._update_controls()

    def poll_events(self):
        if self.closed:
            return
        for _ in range(200):
            try:
                event = self.events.get_nowait()
            except Empty:
                break
            if event.session != self.session_id:
                continue
            if event.kind == "connected":
                self.history.clear()
                self.config_values_confirmed = False
                self.connecting = False
                self.port_open = True
                self.connection_text.set(f"{event.text} 已打开 · 等待设备回复")
                self.append_log("连接", self.connection_text.get())
                for command in ("STREAM ON", "GET", "CONFIG"):
                    self.send(command)
            elif event.kind == "line":
                self.handle_line(event.text, event.received_at, event.wall_time)
            elif event.kind == "sending":
                self.action_text.set(f"等待回复：{event.command}")
                self.append_log("发", event.command, "send")
            elif event.kind == "ack":
                self.action_text.set(f"设备已确认：{event.command}")
            elif event.kind == "rejected":
                self.action_text.set(f"设备拒绝 {event.command}：{event.text}；后续排队操作已取消。")
            elif event.kind == "timeout":
                self.device_ready = False
                self.device_mode = None
                self.connection_text.set("串口仍打开 · 设备无回复")
                self.show_error(f"{event.command} 超时：{event.text}。可点查询状态重试。")
            elif event.kind == "error":
                self.show_error(f"串口错误：{event.text}。若提示拒绝访问，请关闭旧串口助手。")
            elif event.kind == "cancelled":
                self.append_log("队列", event.text, "error")
            elif event.kind == "disconnected":
                self.stop_recording()
                self.connecting = False
                self.port_open = False
                self.device_ready = False
                self.device_mode = None
                self.latest_config = None
                self.config_values_confirmed = False
                self.last_status_time = None
                self.connection_text.set("未连接 · 串口已释放")
                self.data_time_text.set("已断开，屏幕数值已清除")
                for variable in self.values.values():
                    variable.set("—")
                self.config_text.set("未连接，设备参数尚未确认")
            self._update_controls()
        if self.port_open and self.last_status_time is not None:
            age = time.monotonic() - self.last_status_time
            if age > 3:
                self.data_time_text.set(f"状态已 {age:.0f} 秒未更新，可点查询状态；周期上报可能已暂停。")
                for label in self.value_labels:
                    label.configure(foreground="#808080")
            else:
                self.data_time_text.set(f"更新时间 {self.last_status_clock} · 串口接收错误计数 {self.rx_errors}")
        now = time.monotonic()
        if now - self.last_chart_draw >= .25:
            self.trend.redraw(now)
            self.last_chart_draw = now
        self.poll_job = self.root.after(50, self.poll_events)

    def callback_error(self, exc_type, exc_value, traceback):
        logging.error("GUI callback failed", exc_info=(exc_type, exc_value, traceback))
        self.show_error(f"界面操作异常：{exc_value}，详情已记入 app_error.log")

    def close(self):
        if self.closed:
            return
        self.closed = True
        self.stop_recording()
        if self.transport:
            self.transport.stop()
            self.transport.join(timeout=1)
        self.root.after_cancel(self.poll_job)
        self.root.destroy()


def main():
    logging.basicConfig(filename=Path(__file__).with_name("app_error.log"), level=logging.ERROR, encoding="utf-8")
    root = tk.Tk()
    ControlApp(root)
    root.mainloop()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        logging.exception("Application startup failed")
        raise
