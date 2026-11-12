# V1.8 串口文本协议与手动测试

接口为 USART3：PB10 发、PB11 收；电脑使用 9600、8N1、无流控。发送与接收均选文本模式。

## 如何结束一条命令

每条命令必须以真实的回车或换行结束，支持 CR、LF、CRLF。命令后在发送框里按回车，再点击发送；如果软件带“发送新行”选项，也可以使用该选项。不要把反斜杠 r、反斜杠 n 当作普通文字输入。
如果无法确认软件是否发送换行，可临时用 HEX 发送 GET 加 CRLF：47 45 54 0D 0A。正常应收到 STATUS 状态行。

命令大小写不敏感，可使用空格或制表符分隔，空行忽略。单行最多 63 个字符，不含结束符。未换行的部分命令空闲约 1 秒后被丢弃，不会自动执行。

## 命令

| 命令示例 | 行为和回复 |
| --- | --- |
| GET | 立即返回一行 STATUS，不受 STREAM 开关影响 |
| HELP | 返回 OK HELP 及命令摘要 |
| MODE AUTO | 光照控制，返回 OK MODE AUTO |
| MODE LOCAL | 电位器控制，返回 OK MODE LOCAL |
| MODE REMOTE | 远程控制，返回 OK MODE REMOTE；新进入时先保持当前 PWM 输出作为目标 |
| PWM 40 | 仅 REMOTE 下接受，目标设为 40%，返回 OK PWM 40；实际输出仍逐步跟随 |
| STREAM OFF | 停止周期状态行，返回 OK STREAM OFF；控制、采集和显示继续运行 |
| STREAM ON | 恢复约每 500ms 上报，返回 OK STREAM ON |
| CONFIG | 查询当前参数和保存状态，返回 CONFIG,Dark=…,Bright=…,RampMs=…,Storage=FLASH,Saved=…,Dirty=…,Seq=… |
| CAL 3764 481 | 同时设置暗、亮 ADC 参考值，返回 OK CAL 3764 481 |
| RAMP 100 | 每改变 1 个占空比百分点间隔约 100ms，返回 OK RAMP 100 |
| DEFAULTS | 恢复默认暗参考3764、亮参考481、渐变40ms/百分点，返回 OK DEFAULTS 和 CONFIG 行 |
| SAVE | 保存当前校准值和渐变速度，返回 OK SAVE 及 CONFIG；相同配置返回 OK SAVE UNCHANGED |
| LOAD | 恢复最近一次有效保存，返回 OK LOAD 及 CONFIG；没有有效记录时返回错误 |

PWM 只接受 0～100 的十进制整数，不接受正负号、小数、百分号或多余参数。在 AUTO/LOCAL 下发送合法 PWM 数值返回 ERR MODE_USE_REMOTE，不会偷偷切换模式。非法命令不会修改现有控制状态。
按键保持 AUTO → LOCAL → AUTO 的操作；在 REMOTE 中按键直接回到 AUTO。按键切换另发 EVENT MODE=模式名。STREAM OFF 不屏蔽按键事件。
CAL 两个参数的顺序是暗、亮，要求 0≤亮<暗≤4095，且跨度至少 100 个 ADC 计数；跨度限制用于避免过窄参考区间放大波动。非法参数不会部分写入。修改后重新开始光敏滤波，并保持当前控制模式。
RAMP 支持 10～200ms/占空比百分点，默认 40；数值越大，亮度变化越慢。换速不会瞬间跳到新目标。DEFAULTS 不切换模式、不改变远程亮度目标，也不改变 STREAM 开关。
CAL、RAMP、DEFAULTS 先修改内存参数，SAVE 才会持久化。LOAD 只恢复校准值和速度，不改变模式。上电自动 LOAD，有效记录缺失或校验失败时使用默认参数，启动模式固定为 AUTO。

CONFIG 的 Saved=1 表示当前会话已读取或保存了有效 Flash 记录；Dirty=1 表示当前参数尚未保存（或没有任何有效记录），Dirty=0 表示与已保存记录一致。Seq 是保存序号。Storage=UNAVAILABLE 表示芯片容量/型号检查不符合此版本的存储布局，禁止写入。

## 状态行

格式示例（数值仅用于说明）：

```text
STATUS,Mode=REMOTE,ADC=2000,AVG=2010,mV=1620,Light=53%,Target=40%,PWM=35%,Pot=2048,Knob=50%,Remote=40%,RxErr=0
```

Mode 为当前模式；ADC/AVG 为光敏原始值/四点均值；mV 为估算输入电压；Light 为明暗参考间的相对刻度；Target 为当前模式的目标 PWM；PWM 为已经应用的输出；Pot/Knob 为电位器原始值/百分比；Remote 为保存于 RAM 的远程目标；RxErr 为串口帧错误、硬件溢出或软件接收缓存满的累计次数。传感器转换失败时相关字段为 ERR。

所有回复和状态行以 CRLF 结束。状态行以 STATUS, 开头，成功回复以 OK 开头，错误以 ERR 开头，实体按键模式变化以 EVENT 开头，便于后续上位机区分。

## 错误与恢复

| 回复 | 含义及处理 |
| --- | --- |
| ERR COMMAND | 未知命令；重新发送正确命令 |
| ERR ARGUMENTS | 缺少参数、格式错误、模式名错误或多余参数 |
| ERR RANGE_0_100 | PWM 超范围；原目标不变 |
| ERR MODE_USE_REMOTE | 先发送 MODE REMOTE，再设置 PWM |
| ERR LINE_TOO_LONG | 超过 63 字符，整条残行丢弃到结束符 |
| ERR CHARACTER | 含非法控制字符或非 ASCII 字节，残行丢弃到结束符 |
| ERR INCOMPLETE_NEED_NEWLINE | 部分命令空闲 1 秒仍没有换行；补发完整命令及换行 |
| ERR RX_LOSS_RESEND_LINE | 串口接收错误或缓存溢出；排队字节和残行丢弃，等待空闲至少 1 秒，再发送完整命令 |
| ERR CALIBRATION | 校准值超范围、顺序颠倒或跨度不足100；原参数不变 |
| ERR RAMP_10_200 | 速度不在10～200ms/百分点；原速度不变 |
| ERR NO_SAVED_CONFIG | 没有有效的已保存记录；当前内存参数保持不变 |
| ERR FLASH_UNSUPPORTED | 芯片容量或型号不匹配当前布局，未擦写 |
| ERR FLASH_SAVE | 擦除、写入或回读验证失败；可重试 SAVE，旧有效页不会主动擦除 |
| ERR FLASH_LOAD | 存储读取失败；当前内存参数保持不变 |

错误后的正常命令仍可继续使用，不要求重启。输入速度应受控；同步串口上报会占用主循环时间，缓存溢出时会拒绝残缺命令，不承诺在持续灌入命令时无丢失。

## 第一轮实物验收

每次只发送一条命令并换行，等待回复：

1. GET：收到 STATUS。
2. STREAM OFF：自动滚动停止；再次 GET 仍有回复。
3. MODE REMOTE：OLED 显示 REMOTE，回复 OK MODE REMOTE。
4. PWM 20：T 变为 20%，等待 LED 输出跟随。
5. PWM 80：T 变为 80%，LED 逐渐变亮；转旋钮或遮挡光敏模块不改变该目标。
6. PWM 150、PWM abc：分别返回范围错误和参数错误，目标仍为 80%。
7. 按一下 PB1：回到 AUTO，收到 EVENT MODE=AUTO，目标重新由光照决定。
8. PWM 50：返回 ERR MODE_USE_REMOTE，仍为 AUTO。
9. STREAM ON：周期状态恢复；复位后默认 AUTO 且 STREAM 为 ON。

校准参数和渐变速度可保存到 Flash。模式、远程亮度和 STREAM 开关不保存；断开电脑但不断电时，REMOTE 继续跟随最后有效远程目标，可用按键本地接管。尚未加入心跳检测。

## V1.7 参数配置测试

1. CONFIG：未保存过参数时应为 Dark=3764、Bright=481、RampMs=40；若已存在有效记录，以该记录为准。需要从默认参数开始测试，可先 DEFAULTS，但不必 SAVE。
2. RAMP 100，再 CONFIG：确认 RampMs=100。用 REMOTE 的 PWM 20 → PWM 80 测试，目标变化 60 个百分点，渐变约6秒；RAMP 20 下同样变化约1.2秒。每次从已稳定的输出开始计时，允许主循环和观察误差。
3. CAL 4000 1000，再 CONFIG：确认两项一起修改。AUTO 下相对光照和目标会按新参考重新计算。此组只用于测试命令，不作为实际测量依据。
4. CAL 1000 2000、CAL 1000 950、RAMP 0：分别验证颠倒参考、过窄跨度和非法速度均被拒绝。随后 CONFIG 应与错误指令前一致。
5. DEFAULTS，再 CONFIG：恢复 3764、481、40。
6. 不 SAVE 时复位，会恢复最近保存的参数；如果没有有效记录，恢复默认值。

继续用 HEX 模式时，以下内容已包含 CRLF；接收仍保持文本模式：

| 文本含义 | HEX 内容 |
| --- | --- |
| CONFIG | 43 4F 4E 46 49 47 0D 0A |
| RAMP 100 | 52 41 4D 50 20 31 30 30 0D 0A |
| RAMP 20 | 52 41 4D 50 20 32 30 0D 0A |
| CAL 4000 1000 | 43 41 4C 20 34 30 30 30 20 31 30 30 30 0D 0A |
| CAL 1000 2000 | 43 41 4C 20 31 30 30 30 20 32 30 30 30 0D 0A |
| RAMP 0 | 52 41 4D 50 20 30 0D 0A |
| DEFAULTS | 44 45 46 41 55 4C 54 53 0D 0A |
| SAVE | 53 41 56 45 0D 0A |
| LOAD | 4C 4F 41 44 0D 0A |

## V1.8 保存与恢复验收

1. RAMP 100，再 SAVE，等 OK SAVE 后再发 CONFIG；应为 RampMs=100、Saved=1、Dirty=0。
2. 直接按开发板复位键或断电重上电，不要重新 Download。启动会报告 BOOT CONFIG=FLASH，CONFIG 应仍为 RampMs=100。
3. RAMP 20 不保存，CONFIG 应为 RampMs=20、Dirty=1。LOAD 后应恢复100、Dirty=0，模式不变。
4. 连续两次 SAVE 同一配置：第二次应回复 OK SAVE UNCHANGED，Seq 不变。
5. 测试结束 DEFAULTS，再 SAVE，后续启动恢复原始校准值和默认速度40。

每次发送一条命令并等待回复，尤其不要在保存期间连续发送指令。重新下载固件可能清空保存区，复位验收不能用再次下载替代。

## 解析器主机测试

在 D:/Keil_v5/MDK 下运行（本机已安装 MinGW GCC）：

```powershell
& 'C:/Program Files/mingw64/bin/gcc.exe' -std=c11 -Wall -Wextra -Werror -I Object/p7/System Object/p7/System/CommandProtocol.c Object/p7/tests/test_command_protocol.c -o Object/p7/Objects/test_command_protocol.exe
& Object/p7/Objects/test_command_protocol.exe
& 'C:/Program Files/mingw64/bin/gcc.exe' -std=c11 -Wall -Wextra -Werror -I Object/p7/System Object/p7/System/AppConfig.c Object/p7/tests/test_app_config.c -o Object/p7/Objects/test_app_config.exe
& Object/p7/Objects/test_app_config.exe
& 'C:/Program Files/mingw64/bin/gcc.exe' -std=c11 -O2 -Wall -Wextra -Werror -I Object/p7/System Object/p7/System/ConfigStore.c Object/p7/System/AppConfig.c Object/p7/tests/test_config_store.c -o Object/p7/Objects/test_config_store.exe
& Object/p7/Objects/test_config_store.exe
```

预期分别输出 PASS: 56 protocol cases、PASS: 20 config checks 和 PASS: 233 storage cases。软件测试不代替实物验收。
