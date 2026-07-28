# tmx_blinky —— 最小起步工程（血统 = TI SDK 例程 `gpio_toggle_output` + 后加的 GC9A01）

> **它在本仓库的用途只有两个**：① 开新题时**复制它**当空白工程骨架（`car/` 是整车 26 个源文件，不适合当骨架）；
> ② `car` 那边出问题时，用它证明"工具链 + CMSIS-DAP + 板子"这条链还活着（2026-07-24 首次真机点亮 PB22 就是它）。
> **平时不要在这里写功能代码。** 引脚/参数真值一律看 [`工程事实SSOT.md`](../../../.kiro/steering/工程事实SSOT.md)。
>
> **2026-07-28 已瘦身 27 → 9 个文件**：删掉 `iar/`(8) `keil/`(4) `ticlang/`(3) 与 TI 原版 `README.html`(70KB) —— 我们只走 `gcc/` 路径，
> 那 16 个是 SDK 样板、**全仓 0 引用**（删前已 `git grep` 核过）。要找回：`git checkout <本次commit>^ -- workbench/mspm0/tmx_blinky`。
>
> ⚠️ **两个已知坑，动它之前先知道**：
> 1. **`gc9a01.c` 是旧副本**（11.6KB），`car/gc9a01.c`（14.8KB，含"一次 set_window + 整块像素流"的提速改写）才是现役版本。**别在这里改屏驱动**，改了也不会进 `car`。
> 2. **`一键编译烧录.bat` 里的工具路径是写死的**（这台机恰好对得上），换机会挂。`car/一键编译烧录.bat` 才是自动探测版；换机先跑 `car\env_check.ps1`。
>
> ⬇️ 以下是 TI 原版例程说明，**讲的是 LaunchPad（J5/J6/J7 跳线、RGB LED），不是天猛星底板**，只当引脚参考读。

## Example Summary

Toggles three GPIO pins using HW toggle register.

## Peripherals & Pin Assignments

| Peripheral | Pin | Function |
| --- | --- | --- |
| GPIOB | PB22 | Standard Output |
| GPIOB | PB26 | Standard Output |
| GPIOB | PB27 | Standard Output |
| GPIOB | PB16 | Standard Output |
| SYSCTL |  |  |
| EVENT |  |  |
| DEBUGSS | PA20 | Debug Clock |
| DEBUGSS | PA19 | Debug Data In Out |

## BoosterPacks, Board Resources & Jumper Settings

Visit [LP_MSPM0G3507](https://www.ti.com/tool/LP-MSPM0G3507) for LaunchPad information, including user guide and hardware files.

| Pin | Peripheral | Function | LaunchPad Pin | LaunchPad Settings |
| --- | --- | --- | --- | --- |
| PB22 | GPIOB | PB22 | J27_5 | <ul><li>PB22 can be connected to LED2 Blue<br><ul><li>`J5 ON` Connect to LED2 Blue<br><li>`J15 OFF` Disconnect from LED2 Blue</ul></ul> |
| PB26 | GPIOB | PB26 | J27_8 | <ul><li>PB26 can be connected to LED2 Red<br><ul><li>`J6 ON` Connect to LED2 Red<br><li>`J6 OFF` Disconnect from LED2 Red</ul></ul> |
| PB27 | GPIOB | PB27 | J27_10 | <ul><li>PB27 can be connected to LED2 Green<br><ul><li>`J7 ON` Connect to LED2 Green<br><li>`J7 OFF` Disconnect from LED2 Green</ul></ul> |
| PB16 | GPIOB | PB16 | J2_11 | <ul><li>This pin can be used for testing purposes in boosterpack connector<ul><li>Pin can be reconfigured for general purpose as necessary</ul></ul> |
| PA20 | DEBUGSS | SWCLK | N/A | <ul><li>PA20 is used by SWD during debugging<br><ul><li>`J101 15:16 ON` Connect to XDS-110 SWCLK while debugging<br><li>`J101 15:16 OFF` Disconnect from XDS-110 SWCLK if using pin in application</ul></ul> |
| PA19 | DEBUGSS | SWDIO | N/A | <ul><li>PA19 is used by SWD during debugging<br><ul><li>`J101 13:14 ON` Connect to XDS-110 SWDIO while debugging<br><li>`J101 13:14 OFF` Disconnect from XDS-110 SWDIO if using pin in application</ul></ul> |

### Device Migration Recommendations
This project was developed for a superset device included in the LP_MSPM0G3507 LaunchPad. Please
visit the [CCS User's Guide](https://software-dl.ti.com/msp430/esd/MSPM0-SDK/latest/docs/english/tools/ccs_ide_guide/doc_guide/doc_guide-srcs/ccs_ide_guide.html#sysconfig-project-migration)
for information about migrating to other MSPM0 devices.

### Low-Power Recommendations
TI recommends to terminate unused pins by setting the corresponding functions to
GPIO and configure the pins to output low or input with internal
pullup/pulldown resistor.

SysConfig allows developers to easily configure unused pins by selecting **Board**→**Configure Unused Pins**.

For more information about jumper configuration to achieve low-power using the
MSPM0 LaunchPad, please visit the [LP-MSPM0G3507 User's Guide](https://www.ti.com/lit/slau873).

## Example Usage
Compile, load and run the example.
RGB LEDs will toggle with red being opposite of blue and green.

USER_TEST_PIN GPIO will mimic the behavior of the LED1 and LED3 pins on the
BoosterPack header and can be used to verify the LED behavior.
