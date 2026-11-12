"""V1.8 设备协议；本文件不依赖 GUI 或串口硬件。"""

from dataclasses import dataclass, field
import re

MODES = {"AUTO", "LOCAL", "REMOTE"}


@dataclass(frozen=True)
class Message:
    kind: str
    text: str
    fields: dict[str, str] = field(default_factory=dict)


def wire_command(text: str) -> bytes:
    """只接受一条 ASCII 命令，统一补一组 CRLF。"""
    text = text.strip(" \t\r\n")
    if not text:
        raise ValueError("请输入命令")
    if any((ord(c) < 32 and c != "\t") or ord(c) == 127 for c in text):
        raise ValueError("一次只能发送一条命令，不能包含换行或控制字符")
    try:
        payload = text.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("设备命令只接受英文、数字和空格") from exc
    if len(payload) > 63:
        raise ValueError("命令不能超过 63 个字符")
    return payload + b"\r\n"


def integer(text: str, low: int, high: int, label: str) -> int:
    text = str(text).strip()
    if not re.fullmatch(r"[0-9]+", text) or len(text) > 10:
        raise ValueError(f"{label}需要填写整数")
    value = int(text)
    if not low <= value <= high:
        raise ValueError(f"{label}范围为 {low}～{high}")
    return value


def calibration_command(dark: str, bright: str) -> str:
    d = integer(dark, 0, 4095, "暗参考")
    b = integer(bright, 0, 4095, "亮参考")
    if d - b < 100:
        raise ValueError("暗参考必须大于亮参考，且至少相差 100")
    return f"CAL {d} {b}"


def ramp_command(value: str) -> str:
    return f"RAMP {integer(value, 10, 200, '渐变间隔')}"


def pwm_command(value: str) -> str:
    return f"PWM {integer(value, 0, 100, '亮度')}"


def _fields(text: str) -> dict[str, str]:
    result = {}
    for item in text.split(",")[1:]:
        if "=" not in item:
            raise ValueError("设备数据字段缺少等号")
        key, value = item.split("=", 1)
        if not key or not value or key in result:
            raise ValueError("设备数据存在空字段或重复字段")
        result[key] = value
    return result


def _number(value: str, high: int, *, percent=False, error=False) -> None:
    if error and value == "ERR":
        return
    if percent:
        if not value.endswith("%"):
            raise ValueError("设备百分比缺少 %")
        value = value[:-1]
    integer(value, 0, high, "设备数值")


def parse_line(text: str) -> Message:
    text = text.strip("\r\n")
    if text.startswith("STATUS,"):
        values = _fields(text)
        required = {"Mode", "ADC", "AVG", "mV", "Light", "Target", "PWM", "Pot", "Knob", "Remote", "RxErr"}
        if not required <= values.keys() or values["Mode"] not in MODES:
            raise ValueError("状态数据不完整或模式无效，请确认固件为 V1.8")
        for name in ("ADC", "AVG", "Pot"):
            _number(values[name], 4095, error=True)
        _number(values["mV"], 3300, error=True)
        for name in ("Light", "Knob"):
            _number(values[name], 100, percent=True, error=True)
        for name in ("Target", "PWM", "Remote"):
            _number(values[name], 100, percent=True)
        _number(values["RxErr"], 0xFFFFFFFF)
        return Message("status", text, values)
    if text.startswith("CONFIG,"):
        values = _fields(text)
        required = {"Dark", "Bright", "RampMs", "Storage", "Saved", "Dirty", "Seq"}
        if not required <= values.keys():
            raise ValueError("参数数据不完整，请确认固件为 V1.8")
        calibration_command(values["Dark"], values["Bright"])
        ramp_command(values["RampMs"])
        if values["Storage"] not in {"FLASH", "UNAVAILABLE"}:
            raise ValueError("未知的参数存储类型")
        _number(values["Saved"], 1)
        _number(values["Dirty"], 1)
        _number(values["Seq"], 0xFFFFFFFF)
        return Message("config", text, values)
    if text.startswith("OK "):
        return Message("ok", text)
    if text.startswith("ERR ") or text.startswith("HOST_ERR "):
        return Message("error", text)
    if text.startswith("EVENT MODE="):
        mode = text.partition("=")[2]
        if mode not in MODES:
            raise ValueError("按键事件包含无效模式")
        return Message("mode", text, {"Mode": mode})
    return Message("info", text)


def matches_reply(command: str, line: str) -> bool:
    """当前固件没有请求ID：用回复类型和命令关键字匹配。"""
    if line.startswith("ERR "):
        return True
    words = command.upper().split()
    if words[0] == "GET":
        return line.startswith("STATUS,")
    if words[0] == "CONFIG":
        return line.startswith("CONFIG,")
    if words[0] == "MODE" and len(words) == 2:
        return line == f"OK MODE {words[1]}"
    if words[0] == "STREAM" and len(words) == 2:
        return line == f"OK STREAM {words[1]}"
    if words[0] in {"PWM", "RAMP", "CAL"}:
        try:
            expected = "OK " + words[0] + " " + " ".join(str(int(value)) for value in words[1:])
        except ValueError:
            return False
        return line == expected
    return line.startswith(f"OK {words[0]} ") or line == f"OK {words[0]}" or line.startswith(f"OK {words[0]}:")


class LineFramer:
    """保留跨多次 read 的半行；超长或非ASCII行丢弃后可恢复。"""

    def __init__(self, limit=1024):
        self.limit = limit
        self.buffer = bytearray()
        self.dropping = False

    def feed(self, data: bytes) -> list[str]:
        lines = []
        for byte in data:
            if byte in (10, 13):
                if self.buffer and not self.dropping:
                    lines.append(self.buffer.decode("ascii"))
                self.buffer.clear()
                self.dropping = False
            elif not self.dropping:
                if not (32 <= byte <= 126 or byte == 9):
                    lines.append("HOST_ERR NON_ASCII_LINE")
                    self.buffer.clear()
                    self.dropping = True
                elif len(self.buffer) >= self.limit:
                    lines.append("HOST_ERR LINE_TOO_LONG")
                    self.buffer.clear()
                    self.dropping = True
                else:
                    self.buffer.append(byte)
        return lines
