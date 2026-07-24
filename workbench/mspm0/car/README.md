# car —— 天猛星小车起步工程（双电机 PWM + GC9A01 仪表盘）

MSPM0G3507（天猛星主板）小车底盘起步固件：上电后初始化 GC9A01 圆屏当仪表盘，
用 TIMA0 四路 PWM 驱动两个 DRV8231 有刷电机，循环演示 **前进 / 停 / 转弯 / 后退**，
屏上实时显示当前动作与两电机占空比，板载 PB22 LED 心跳闪烁。

> 🔴 **代码位置铁律**：本工程放在仓库内**全 ASCII 路径** `workbench/mspm0/car/`。
> GCC 工具链不认中文路径（`device.opt ... not found`）——**不要把工程移到中文目录**。
> `C:\ti` 只放工具（SDK / 编译器 / OpenOCD / SysConfig）。

## 引脚分配

| 功能 | 信号 | 引脚 | 说明 |
| --- | --- | --- | --- |
| 电机 M1 | IN1 / IN2 | PA8 / PA9 | TIMA0_C0 / TIMA0_C1 |
| 电机 M2 | IN1 / IN2 | PB12 / PB13 | TIMA0_C2 / TIMA0_C3 |
| 显示 | SCL / SDA | PB9 / PB8 | SPI1_SCK / SPI1_MOSI @8MHz |
| 显示 | RES / DC / CS / BLK | PB10 / PB11 / PB14 / PB26 | GPIO 手控 |
| 状态灯 | STATUS_LED | PB22 | 板载蓝 LED，心跳 |
| 下载 | SWCLK / SWDIO | PA20 / PA19 | 外置 CMSIS-DAP SWD |

PWM：TIMA0，周期计数 `MOTOR_PWM_PERIOD=1600`（约 20kHz，静音）。

## 接线（DRV8231 模块 + 电机）

一路电机对应一片 DRV8231：

- **H1（输入侧）**：`+12V`（电机电源） / `IN1` / `+3.3V`（逻辑，接天猛星 3V3） / `IN2`
  - M1：IN1←PA8，IN2←PA9；M2：IN1←PB12，IN2←PB13
- **H2（输出侧）**：`GND` / `ADC`（电流采样，本工程暂不用） / `OUT1` / `OUT2` → 接电机两端
- **共地**：DRV8231 的 GND 必须与天猛星 GND 相连。

> ⚠ 没接电机时，屏上照样会切换动作（PWM 在发，只是没负载看不到转动），可用来先验证显示与固件在跑。

## 双向控制约定（motor.c）

```c
motor_set(MOTOR_M1, 40);   // +：正转  IN1=PWM, IN2=0
motor_set(MOTOR_M2, -40);  // -：反转  IN1=0,  IN2=PWM
motor_set(MOTOR_M1, 0);    // 0：滑行  两路都 0
motor_stop_all();          // 两电机滑行停
```

`duty` 范围 `-100..100`。演示动作表在 `car.c` 的 `steps[]`，可自行改。

## 编译

在 `workbench\mspm0\car\gcc\` 下（PATH 需含 `...\mingw64\bin` 与 arm-none-eabi-gcc）：

```powershell
mingw32-make
```

产物 `car.out`（当前 text≈12.8KB）。改了 `car.syscfg` 后 make 会先跑 SysConfig 重生成
`ti_msp_dl_config.c/.h`（较慢，属正常）。也可 VS Code 打开本目录 `Ctrl+Shift+B`。

## 烧录（外置 CMSIS-DAP，只接 SWD：DIO/CLK/GND）

节奏铁律：**烧一次 → 按板载 RST → 看现象**，别连续快烧（会把 MCU 怼进 lockup）。

```powershell
openocd -s C:/ti/xpack-openocd-0.12.0-7/openocd/scripts `
  -f interface/cmsis-dap.cfg -c "adapter speed 500" `
  -f target/ti_mspm0.cfg `
  -c "program car.out reset exit"
```

看到 `** Programming Finished **` + `Resetting Target` 即成功；屏显 `CAR DEMO` 开始循环。

> ⚠️ 用 **500kHz + 不带 verify**。@1000kHz 的 `verify` 偶发 `timed out while waiting for target halted`
> （CRC 校验要 halt CPU 跑校验程序，SWD 时序抖动超时）→ Verify Failed 且不复位；但 flash **已写入成功**。
> 开发期写完直接 `reset` 跑，看屏/看串口 log 就知道对不对，不必每次 CRC 校验。

### 万一烧不进 / 报 `Could not find MEM-AP`（救砖）

连续快烧后 MCU 可能进 double-fault lockup。用工厂复位一条龙，**执行时手点一下 RST**：

```powershell
openocd -s C:/ti/xpack-openocd-0.12.0-7/openocd/scripts `
  -f interface/cmsis-dap.cfg -c "adapter speed 1000" `
  -f target/ti_mspm0.cfg `
  -c "init; mspm0_factory_reset; flash write_image erase car.out; verify_image car.out; reset run; shutdown"
```

## 预期现象

上电/复位后：背光亮 → 屏顶 `CAR DEMO`（绿）→ 中间大字显示动作
（`FORWARD`→`STOP`→`TURN`→`STOP`→`REVERSE`→`STOP` 循环）→ 下方 `M1:+40 M2:-40` 占空行；
PB22 蓝灯按 150ms 心跳闪。接了电机则同步正转 / 原地转 / 后退。
