# BSP Ownership

`BSP` owns hardware-facing code and generated SysConfig names. Application code must not manipulate MSPM0 registers or generated pin macros directly.

Planned modules:

- `bsp_timebase.c/.h`: 1 ms system tick and timing helpers; owns the timer ISR.
- `bsp_motor.c/.h`: TB6612 direction, PWM, standby, output limiting, and emergency stop.
- `bsp_encoder.c/.h`: left hardware QEI and right GPIO edge decoding; owns encoder ISRs.
- `bsp_gray.c/.h`: 74HC4051 addressing, ADC sampling, calibration data, and raw channel values.
- `bsp_uart.c/.h`: UART0 debug/command port and reserved UART1 transport; owns UART ISRs.
- `bsp_imu.c/.h`: ICM-45686 I2C transport, identity/configuration checks, and 200 Hz polling samples.
- `bsp_tft.c/.h`: ST7735S SPI transport and display primitives.
- `bsp_buttons.c/.h`: debounced button inputs.
- `bsp_buzzer.c/.h`: audible/visual point prompts.

Interrupt handlers stay with the BSP module that owns the peripheral. They should only capture data/set flags and return quickly; control algorithms run from the application scheduler.
