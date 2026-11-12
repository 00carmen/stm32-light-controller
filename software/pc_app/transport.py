"""串口线程拥有端口，Tk 主线程只通过队列收发事件。"""

from dataclasses import dataclass, field
from datetime import datetime
from queue import Queue, Empty
import threading
import time
import serial
from serial.tools import list_ports

from protocol import LineFramer, matches_reply, parse_line, wire_command


@dataclass(frozen=True)
class TransportEvent:
    session: int
    kind: str
    text: str = ""
    command: str = ""
    received_at: float = field(default_factory=time.monotonic)
    wall_time: datetime = field(default_factory=lambda: datetime.now().astimezone())


def available_ports():
    return [(p.device, p.description) for p in list_ports.comports()]


class SerialSession(threading.Thread):
    def __init__(self, port, events, session=1, serial_factory=serial.Serial, reply_timeout=3.0):
        super().__init__(name=f"serial-{session}", daemon=True)
        self.port = port
        self.events = events
        self.session = session
        self.serial_factory = serial_factory
        self.reply_timeout = reply_timeout
        self.commands = Queue(maxsize=32)
        self.enqueue_lock = threading.Lock()
        self.stop_event = threading.Event()

    def emit(self, kind, text="", command=""):
        self.events.put(TransportEvent(self.session, kind, text, command))

    def send(self, text):
        self.send_many([text])

    def send_many(self, commands):
        packets = [wire_command(text) for text in commands]
        with self.enqueue_lock:
            if self.stop_event.is_set():
                raise ValueError("串口连接已关闭")
            if self.commands.qsize() + len(packets) > self.commands.maxsize:
                raise ValueError("待发送命令过多，请等待设备回复")
            for packet in packets:
                self.commands.put_nowait((packet[:-2].decode("ascii"), packet))

    def stop(self):
        self.stop_event.set()

    def _cancel_queue(self):
        count = 0
        while True:
            try:
                self.commands.get_nowait()
                count += 1
            except Empty:
                break
        if count:
            self.emit("cancelled", f"已取消后续 {count} 条排队命令")

    def run(self):
        port = None
        pending = None
        framer = LineFramer()
        try:
            port = self.serial_factory()
            port.port = self.port
            port.baudrate = 9600
            port.bytesize = serial.EIGHTBITS
            port.parity = serial.PARITY_NONE
            port.stopbits = serial.STOPBITS_ONE
            port.timeout = 0.05
            port.write_timeout = 0.5
            port.xonxoff = port.rtscts = port.dsrdtr = False
            port.dtr = port.rts = False
            port.open()
            port.reset_input_buffer()
            self.emit("connected", self.port)
            while not self.stop_event.is_set():
                if pending is None:
                    try:
                        command, packet = self.commands.get_nowait()
                    except Empty:
                        pass
                    else:
                        if self.stop_event.is_set():
                            break
                        written = port.write(packet)
                        if written != len(packet):
                            raise serial.SerialException("命令未完整写入串口")
                        pending = (command, time.monotonic() + self.reply_timeout)
                        self.emit("sending", command, command)

                data = port.read(min(max(port.in_waiting, 1), 512))
                for line in framer.feed(data):
                    self.emit("line", line)
                    valid = True
                    try:
                        message = parse_line(line)
                        valid = message.kind != "info"
                    except ValueError:
                        valid = False
                    if pending and valid and matches_reply(pending[0], line):
                        failed = line.startswith("ERR ")
                        self.emit("rejected" if failed else "ack", line, pending[0])
                        pending = None
                        if failed:
                            self._cancel_queue()
                if pending and time.monotonic() >= pending[1]:
                    self.emit("timeout", "设备未在 3 秒内回复，请检查供电、接线和固件", pending[0])
                    pending = None
                    self._cancel_queue()
        except (serial.SerialException, OSError) as exc:
            self.emit("error", str(exc))
        except Exception as exc:
            self.emit("error", f"串口线程异常：{exc}")
        finally:
            self._cancel_queue()
            if port is not None:
                try:
                    if port.is_open:
                        port.close()
                except (serial.SerialException, OSError):
                    pass
            self.emit("disconnected")
