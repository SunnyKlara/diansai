# UART 调试协议规范 —— 电赛装置标准命令集

> 这是从 2024 B 实战中沉淀的协议。**所有需要"算法验证 / 校准 / 调参"的题目都应该实现这个协议**。
> 协议小、易实现、与 PC 端 cal_helper.py 配套。

---

## 一、为什么需要标准协议

电赛装置的"输入"是物理量（电压、电流、传感器读数），"输出"是显示+蜂鸣+串口。

**没有标准协议时**：
- bug → 改代码 → 烧 → 看 OLED → 改 → 烧 → 看 OLED ……（10min/轮）
- 校准 → 万用表读 → 心算系数 → 改宏 → 重编译 → 烧（30min）
- 现场异常 → 不知道是算法还是采样（无解）

**有标准协议时**：
- bug → `STAT` 看实时数据（1s）
- 校准 → `CAL V=220 I=2.2`（10s 持久化到 FRAM）
- 现场异常 → `DUMP` 导出原始 → PC 跑金标准（30s）

---

## 二、标准命令集（**必须**实现的 4 条）

### `STAT\r\n` — 实时状态

**装置应答**：`<key>=<value> <key>=<value> ...\r\n`

**示例**：
```
> STAT
< V=220.123 I=2.205 P=485.6 PF=0.998 THD=0.123
```

**实现要点**：
- 单行打印，键值对用空格隔开
- 浮点用 `%.3f` 或 `%.4f`（方便 PC 端 regex 提取）
- 不要在测量循环内调用，避免阻塞

---

### `CAL <key>=<value> [<key>=<value> ...]\r\n` — 校准

**装置应答**：`OK CAL <gain1>=<val> <gain2>=<val>` 或 `ERR CAL: <reason>`

**示例**：
```
> CAL V=220.0 I=2.20
< OK CAL v_gain=0.080612 i_gain=0.000805
```

**实现要点**：
- 用"上次 STAT 输出 vs 用户给定真值"反推校准系数
- 写入 FRAM / Flash 持久化
- 反推前检查：上次测量值不能为 0（否则除零）

---

### `DUMP\r\n` — 导出原始采样

**装置应答**：
```
DUMP_START\r\n
<数据 1>\r\n
<数据 2>\r\n
...
DUMP_END\r\n
```

**示例**：
```
> DUMP
< DUMP_START
< 2048,2050
< 2055,2052
< ...（共 1000 行）
< DUMP_END
```

**实现要点**：
- 用 `DUMP_START` / `DUMP_END` 包裹，PC 端可靠解析
- 单行内多个值用逗号分隔（CSV 格式）
- 数据量大时考虑分块发送 + ACK
- 这是最关键的命令，**没它就没法做"算法 vs 硬件"分离诊断**

---

### `RST\r\n` — 出厂复位

**装置应答**：`OK RST: factory defaults restored\r\n`

**实现要点**：
- 写入 `V_GAIN_INIT` 等 config.h 初值
- 不要清除其他状态（如显示页）

---

## 三、可选命令（按需实现）

### `SET <key>=<value>` — 调参

```
> SET freq=60
< OK SET freq=60.000
```

适用：可变频率 / 可变量程的题目（如 2024 B 多频率，2025 A 频率切换）。

### `LOG ON/OFF` — 实时日志

```
> LOG ON
< OK LOG started @ 1Hz
< [1234ms] V=220.0 I=2.2 P=484
< [2234ms] V=220.1 I=2.2 P=484
```

适用：调试 PI 参数时连续看响应曲线。

### `PING` — 心跳测试

```
> PING
< PONG
```

PC 端用来确认串口连接 OK。

---

## 四、装置端实现模板（C 代码）

### 头文件

```c
// uart_dbg.h
void UART_Init(uint32_t baud);
void UART_PutString(const char *s);
void UART_Printf(const char *fmt, ...);
void UART_PollCommands(void);

// main 在每次测量结束后调用，把结果存进 UART 模块
void UART_UpdateLastResult(...);
```

### 命令分发框架（uart_dbg.c）

```c
static volatile char    s_cmd_buf[64];
static volatile uint8_t s_cmd_idx = 0;
static volatile uint8_t s_cmd_ready = 0;

// 串口 RX 中断里：
void UART_OnRxByte(char c) {
    if (s_cmd_ready) return;
    if (c == '\r') return;
    if (c == '\n' || s_cmd_idx >= 63) {
        s_cmd_buf[s_cmd_idx] = '\0';
        s_cmd_ready = 1;
        return;
    }
    s_cmd_buf[s_cmd_idx++] = c;
}

void UART_PollCommands(void) {
    if (!s_cmd_ready) return;
    const char *cmd = (const char *)s_cmd_buf;
    
    if      (strncmp(cmd, "STAT", 4) == 0) handle_stat();
    else if (strncmp(cmd, "CAL ", 4) == 0) handle_cal(cmd + 4);
    else if (strncmp(cmd, "DUMP", 4) == 0) handle_dump();
    else if (strncmp(cmd, "RST",  3) == 0) handle_rst();
    else if (strncmp(cmd, "PING", 4) == 0) UART_PutString("PONG\r\n");
    else                                    UART_PutString("ERR: unknown cmd\r\n");
    
    s_cmd_idx = 0;
    s_cmd_ready = 0;
}
```

### main 集成

```c
int main(void) {
    UART_Init(115200);
    Calib_Load();
    
    while (1) {
        run_measure_cycle();        // 完整测量周期
        UART_UpdateLastResult(...); // 把结果交给 UART 模块
        UART_PollCommands();        // 处理积压命令
        // 进 LPM3 等下一秒
    }
}
```

---

## 五、波特率与连接

| 场景 | 波特率 |
|---|---|
| 命令交互（STAT / CAL） | **115200** |
| 大量数据 DUMP（<10kB） | **115200**（32s 传完 1024 点 × 5 字节）|
| 大量数据 DUMP（>10kB） | **921600** 或换 USB CDC |

**默认 115200**，足够大多数电赛场景。

---

## 六、PC 端配套（cal_helper.py）

见 `01_PC验证脚手架.md` 第二节。每条命令对应一个 `do_xxx()` 子命令，全部走同一份串口代码。

---

## 七、5 题中的实现状态

| 题目 | UART 协议规范 | cal_helper.py | 完成度 |
|---|---|---|---|
| 2024 B | ✅ STAT/CAL/DUMP/RST | ✅ | 100% |
| 2024 H | ✅ STAT/PID/TASK/CALIB IMU/RST | ✅ 含 PC 仿真模式 | 100% |
| 2025 A | ✅ STAT/PID/SET FREQ-MOD/SR/RST | ✅ 含母线电压陷阱诊断 | 100% |
| 2025 G | ✅ STAT/LEARN/DUMP/RST | ✅ 含 Bode 画图 / 滤波器分类 | 100% |
| 2021 A | ✅ STAT/MEASURE/DUMP/RST | ✅ 含端到端 verify | 100% |

**所有 5 题都打通了"装置 ↔ PC 端调试链路"**。

---

## 八、反模式

❌ 用 `printf` 直接打印（没有命令交互能力）
❌ 协议不带 START/END 标记（PC 端解析不可靠）
❌ DUMP 命令在测量循环里阻塞（OLED 黑屏 30s 评委以为死机）
❌ 校准结果只存 RAM（断电丢失）
❌ 串口波特率不一致（队员之间混乱）
