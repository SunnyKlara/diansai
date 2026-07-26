# Bring-Up Log

## 2026-07-09 Initial Agent Setup

- Project found at `C:\Users\3545\workspace_ccstheia\2024hVibe`.
- `empty.syscfg` contains SYSCTL and Board only. No pins are assigned yet.
- Generated header declares `SYSCFG_DL_init()`.
- Static check succeeded.
- Static check warning: `.syscfg` does not include `@versions`; this is acceptable for the current empty template but should be watched after SysConfig edits.
- Build output exists at `Debug\2024hVibe.out`.
- `targetConfigs\MSPM0G3507.ccxml` uses SEGGER J-Link.
- Probe detection found Horco CMSIS-DAP / DAPLink on COM3; OpenOCD is the recommended backend for the connected probe.
- CCS-generated build succeeded with `C:\ti\ccs2020\ccs\utils\bin\gmake.exe -C Debug clean all`.
- OpenOCD probe succeeded at 24000 kHz using `interface/cmsis-dap.cfg` and `target/ti_mspm0.cfg`.
- OpenOCD flash of `Debug\2024hVibe.out` succeeded and ended with `reset run`.
- OpenOCD printed `checksum mismatch - attempting binary compare`, but the helper categorized the flash/verify operation as success.

## Next Physical Checks

- Confirm whether COM3 is only DAPLink debug or also UART bridge.
- Send TB6612 module manual and Tianmengxing pinout before assigning motor pins.
- Send grayscale sensor manual before choosing GPIO vs ADC inputs.
- Keep wheels lifted for the first motor PWM test.

## 2026-07-09 Module Manual Review

- Copied local manuals to `module_manuals\gray8_sensor.pdf` and `module_manuals\tb6612_module.pdf` to avoid Chinese-path tooling issues.
- TB6612 manual confirms `PWMA/PWMB`, `AIN1/AIN2`, `BIN1/BIN2`, `STBY`, `VM`, `VCC`, and common ground requirements; recommended PWM frequency is 10 kHz; `STBY=1` enables the driver.
- Grayscale manual confirms the sensor uses 74HC4051 channel selection, not eight independent outputs. It needs one ADC input for `OUT`, GPIO outputs for `AD0/AD1/AD2`, and optional `EN` low-enable control.
- First wiring plan selected in `docs\hardware_interface.md`; `empty.syscfg` has not been changed yet.

## 2026-07-10 UART, IMU, Display, And Layering Update

- Primary UART0 changed to the user-selected Tianmengxing pair: `PA10` TX and `PA11` RX. These board-special pins are dedicated to their intended UART use.
- Reserved UART1 on `PA8` TX and `PA9` RX for future device communication.
- Added the ICM-45686 bare-chip datasheet. Planned module connection is I2C on `PB2/PB3`, address `0x68`, INT1 on `PB24`, and INT2 on `PB25`. Module supply remains 3.3 V pending a module-board schematic.
- Added ST7735S schematic, controller datasheet, and vendor initialization sequence. Reserved SPI pins `PB9/PB8` plus `PB12-PB15` control signals.
- Added `BSP\` and `APP\` ownership documentation. Root `empty.c` remains main; peripheral ISRs belong to their BSP drivers.

## 2026-07-10 Motor, Encoder, Grayscale, And UART Bring-Up

- Configured TB6612 direction/standby GPIO, TIMA0 10 kHz dual PWM, four encoder GPIO edge interrupts, grayscale ADC/4051 addressing, UART0 at 115200, PB21 button, PB22 status LED, and a TIMG12 1 ms tick in `empty.syscfg`.
- Implemented BSP drivers and an APP bring-up state machine. Both encoders use software quadrature decoding and a 10 ms sample window.
- Added `tools\build.ps1` because the installed CCS headless build application printed usage instead of regenerating source-directory makefiles. The script uses the same SysConfig, TI Arm Clang, `device.opt`, and linker script as the CCS project.
- Full compile/link succeeded. The map contains `GROUP1_IRQHandler`, `TIMG12_IRQHandler`, `UART0_IRQHandler`, and all BSP/APP modules.
- OpenOCD initially programmed the firmware but reported a verify mismatch near `0x1A18`; subsequent low-speed retries encountered CMSIS-DAP timeout/command corruption. The programmed firmware nevertheless reset and ran normally.
- COM3 UART TX was physically observed at 115200. The 1 ms clock, 500 ms telemetry, and all eight grayscale ADC channels are working.
- UART PC-to-MCU RX is not working: sending `help` leaves the firmware RX byte counter at zero. Check adapter TX to PA11, adapter RX to PA10, and common ground.
- Motors remain disabled at boot. PB21 or the serial `test` command starts a bounded low-duty left/pause/right test; this still needs physical wheel-lifted validation.

## 2026-07-10 Physical Motor, Encoder, And UART Validation

- PB21 produced the expected bounded sequence: left wheel, pause, right wheel, stop. Both motor channels, TB6612 standby, direction GPIO, and PWM were physically validated with the wheels lifted.
- UART0 recovered after power was restored. Sending `help\n` on COM3 returned the command list, and the firmware RX counter advanced by exactly five bytes. PA10 TX and PA11 RX are validated in both directions at 115200 8N1.
- `motor 220 220` ran both wheels for the one-second timeout. Raw software-quadrature totals were approximately left `-2633`, right `+2382`; invalid-transition counters remained `0,0`.
- `motor -220 -220` reversed both encoder signs and returned the cumulative totals close to the starting position; invalid-transition counters again remained `0,0`.
- The left encoder is inverted in the BSP so both public wheel counts are positive for vehicle-forward motion. The observed left/right count ratio at equal PWM was about 1.11, so independent wheel-speed closed loops will be required for straight driving.
- All eight grayscale ADC channels continued updating during motor operation. A static sample was approximately `79,418,643,1261,1329,760,436,111`, confirming ADC and 4051 scanning but not yet line-position calibration.
- The polarity-normalized firmware was rebuilt, flashed over CMSIS-DAP at 1000 kHz, and reset successfully. A repeated `motor 220 220` run reported positive totals `2597,2392` with `0,0` invalid transitions.
- Isolated commands verified channel mapping and no cross-counting: `motor 220 0` changed only the left count, and `motor 0 220` changed only the right count.

## 2026-07-10 Grayscale Calibration Bring-Up

- Added 4051 settling delay, first-conversion discard, four-sample ADC averaging, and a first-order IIR filter.
- An 8-second manual line sweep calibrated all eight channels successfully. Measured black-to-white spans were `403,1055,647,862,834,702,784,585`, which are sufficient despite the sensor's relatively high mounting position.
- With the line centered, the weighted error stayed near zero (`-26` to `+329` on a `-3500` to `+3500` scale) and `lost=0` remained stable.
- Moving the line to the vehicle-left sensors produced negative error around `-1100` to `-1700`; moving it to the vehicle-right sensors produced positive error around `+1360` to `+2090`. Channel order therefore matches vehicle left-to-right.
- The first all-white test incorrectly reported a line because calibration extrema mapped white-background variation to as much as 61 percent blackness. Added a 60 percent blackness dead zone before line weighting and lost-line detection; this requires a repeat calibration and all-white regression test.
- Repeated calibration after the dead-zone change produced valid spans `380,1044,567,848,793,603,763,469` for channels 1-8.
- The all-white regression then remained exactly `norm=0,0,0,0,0,0,0,0`, `str=0`, and `lost=1` for six seconds.
- The centered-line regression remained `lost=0`, with error `+76` to `+236` on the `-3500` to `+3500` scale and strength `1024` to `1665`. This is the accepted baseline for the first line-following controller.
- A distant wireless flash attempt failed verification at 1000 kHz and completed only after falling back to 500 kHz. Bringing the wireless endpoints closer restored stable UART operation; keep them close for future firmware iterations.

## 2026-07-10 Suspended-Wheel Speed PI Bring-Up

- Added independent 10 ms left/right wheel-speed PI loops in `APP/app_speed_control.c`, with signed count targets, one-count-per-cycle target slew, integral limiting, feedforward, output limiting, and a three-second serial-test timeout.
- Initial suspended-wheel gains are `Kp=3.00`, `Ki=0.10` per 10 ms, and feedforward `8.50 PWM/count`. Serial representation is `pi 300 10 850`.
- At target `15,15`, both wheels settled at `15,15 count/10ms`; steady PWM was approximately `147,153`, compensating the mechanical mismatch automatically. Totals differed by about one percent and invalid transitions remained `0,0`.
- At target `25,25`, measurements stayed mostly within `24-26`; PWM stayed around `215-225`, well below the bring-up limit of 400.
- Reverse target `-15,-15` settled correctly with PWM near `-146,-142`, validating signed feedback and feedforward.
- Differential target `12,18` settled at `12,18` with PWM near `120,163`, validating independent control for future line-steering commands.
- A failed wireless flash left the target in an unknown state. Power-cycling the board and reconnecting the wireless probe restored the link; a subsequent full flash and verify at 500 kHz completed without checksum mismatch.

## 2026-07-10 Full-Arc Line-Following Bring-Up

- Stored the validated grayscale calibration as BSP defaults: minima `18,32,57,26,32,41,38,58` and maxima `398,1076,624,874,825,644,801,527`. Runtime sweep calibration remains available.
- Generic reactive PD completed the half-circle but produced a visible low-frequency left/right oscillation and poor exit heading.
- Lowering the blackness floor from 60 to 50 percent restored adjacent-sensor interpolation. A 1/2 error filter reduced sensor quantization, but reactive control alone still oscillated because the known-radius curve required sustained differential speed.
- Encoder totals showed the tested arc needs approximately `-1.66 count/10ms` turn feedforward. `followarc 12 -2` was therefore added, together with encoder wheel-difference yaw damping and gentler line gains `Kp=0.0035`, `Kd=0.0120`.
- The final half-circle run completed in about 7.8 seconds. Error stayed roughly within `-1505` to `+1122`, the last valid error before the line endpoint was `+75`, and physical observation reported the car was particularly stable.
- Added `tools/flash.ps1`: explicit reset-halt before/after write plus a second direct-byte verification session with OpenOCD work area disabled. This avoids the unreliable target-side CRC helper over the wireless DAPLink.
- Temporarily mapped onboard PB21 to the validated `followarc 12 -2` demo. It starts only while a valid line is detected, stops on persistent line loss or after 15 seconds, and pressing PB21 while running stops immediately.
- Repeated physical demonstrations still showed a small discrete left/right correction cycle. Runtime comparison rejected softer `20/80` gains because error expanded to about `+2937/-2908` late in the arc.
- Runtime `40/220` completed the half-circle with error approximately `-1061` to `+1102`, no growing oscillation, and visibly improved stability over `35/120`. This is retained as the qualified fallback before experimenting with fully continuous analog line weighting.

## 2026-07-11 ICM-45686 Bring-Up

- Connected the ICM-45686 module over I2C1 on `PB2/PB3` at address `0x68`; all eight module pins are wired, while the first driver polls instead of relying on `INT1/INT2`.
- Configured accelerometer +/-4 g and gyroscope +/-500 dps at 200 Hz in low-noise mode. Sensor data is parsed in the device's default little-endian format.
- Identity and transport passed: `WHO_AM_I=0xE9`, I2C error count remained zero, and a level stationary sample was approximately `15,-30,1004 mg`.
- Added a 400-sample/two-second stationary gyro-bias calibration and fixed-point relative yaw integration. With vehicle axes X forward, Y left, and Z up, left turns produce positive yaw.
- A manual left turn of approximately 90 degrees measured `+91.728 degrees`, about 1.9 percent from the nominal hand-positioned angle.
- The heading changed from `91.728` to `91.725 degrees` during roughly 50 seconds stationary, after bias and 0.25 dps deadband correction. This validates short-run relative heading for arc exit and turn-angle control; it is not an absolute magnetic heading.
- Added serial commands `imu`, `imu cal`, and `yaw zero`. PC-side capture can miss the immediate first response when COM3 is opened and written in one operation; repeating the command after one second reliably captures it, while MCU RX parsing remains correct.

## 2026-07-11 Continuous Line-Control Comparison

- Added IMU telemetry to the qualified half-arc run without changing its controller. The baseline completed in about 7.8 seconds, measured `175.496 degrees` after stopping, and still showed a small visible left/right correction cycle.
- Two experimental attempts to put gyro Z-rate feedback inside the arc follower were rejected. They increased visible oscillation and ended near `170.517` and `163.804 degrees`; instantaneous gray-line steering requests were changing too quickly to serve as gyro rate targets.
- The root cause of the remaining step-like motion was the integer `count/10ms` wheel-speed target. One steering unit changes the left/right speed difference by two whole encoder counts, producing alternating near-straight and corrective segments.
- Added fixed-point wheel-speed requests and continuous line steering at 0.01 count/10 ms resolution. `followfine 12 -2` keeps the existing gray PD, curve feedforward, encoder damping, speed PI, and safety stops; the IMU is telemetry only.
- The continuous run completed the half-circle and was reported physically as particularly stable, with almost no near-straight-then-sudden-correction behavior. This is the accepted arc controller and is now the temporary PB21 demo default.
- Keep gyro yaw for route-state decisions and unmarked-straight heading hold. Do not feed gyro rate into the line-following arc loop unless a later test provides a separately generated smooth heading reference.
