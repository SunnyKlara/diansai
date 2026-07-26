# Hardware Interface Plan

This file records pin and module decisions. Do not assign final pins until the relevant board or module manual has been checked.

The consolidated hardware-handoff table, including LQFP-64 package pin numbers and power-domain notes, is in `docs/hardware_pinout_final.md`.

## Board

- MCU board: Tianmengxing MSPM0G3507, assumed MSPM0G3507 LQFP-64.
- Special pins to avoid for ordinary assignments: `PA21`, `PA23`, `PA02`, `PA18`, `PA10`, `PA11`.
- Current CCS target config: J-Link.
- Current detected probe during setup: Horco CMSIS-DAP / DAPLink, COM3, OpenOCD backend recommended.

## Recommended First Wiring

This is the recommended pin plan for the complete car. It avoids the Tianmengxing special pins except for the explicitly requested onboard UART0 pair:

- Primary debug / wireless UART0: `PA10` TX, `PA11` RX. These are Tianmengxing special pins and are used only because the user explicitly selected the board's intended UART connection.
- Reserved secondary UART1: `PA8` TX, `PA9` RX.
- ICM-45686 I2C bus: `PB2` SCL, `PB3` SDA.
- ST7735S display SPI bus: `PB9` SCLK, `PB8` MOSI; `PB7` remains reserved as SPI MISO for future devices.
- Keep `PB22` available for onboard LED unless deliberately reused.

### TB6612 Motor Driver

| TB6612 signal | MSPM0 pin | Planned SysConfig role | Wiring note |
| --- | --- | --- | --- |
| `PWMA` | `PB20` | `TIMA0_CCP1` PWM, 10 kHz | Left motor PWM by default. |
| `AIN1` | `PA12` | GPIO output | Direction bit. |
| `AIN2` | `PA13` | GPIO output | Direction bit. |
| `PWMB` | `PB4` | `TIMA0_CCP2` PWM, 10 kHz | Right motor PWM by default. |
| `BIN1` | `PA14` | GPIO output | Direction bit. |
| `BIN2` | `PA15` | GPIO output | Direction bit. |
| `STBY` | `PA16` | GPIO output, default low then high after init | Can also be tied to 3.3 V, but MCU control is safer. |
| `VCC` | `3V3` or module-required logic supply | Logic supply | TB6612 accepts 2.7-5.5 V logic. Prefer 3.3 V if the module permits. |
| `VM` | motor battery path | Motor power | Already connected; verify polarity and common ground. |
| `GND` | `GND` | Common ground | MCU ground and motor driver ground must be common. |

Default motor direction convention:

- Forward: `IN1=1`, `IN2=0`, PWM active.
- Reverse: `IN1=0`, `IN2=1`, PWM active.
- Brake/stop: PWM 0 first, then choose coast/brake in firmware after testing.
- If a wheel runs backward, swap that motor's direction convention in firmware first; only swap motor wires if the mechanical convention is clearer.

### Encoders

| Encoder signal | MSPM0 pin | Planned role | Wiring note |
| --- | --- | --- | --- |
| Left `E1A` | `PA29` | QEI/capture capable input | Preferred hardware QEI pair with `PA30`. |
| Left `E1B` | `PA30` | QEI/capture capable input | Preferred hardware QEI pair with `PA29`. |
| Right `E2A` | `PB0` | GPIO interrupt input | Count both edges in software first. |
| Right `E2B` | `PB1` | GPIO interrupt input | Use B level to determine direction. |
| Encoder `VCC` | module encoder supply | Power | Confirm encoder output voltage before connecting to MSPM0. |
| Encoder `GND` | `GND` | Common ground | Must share MCU ground. |

If both encoders need hardware QEI later, revisit the pin plan after confirming exposed timer/QEI-capable header pins on the Tianmengxing board.

Physical bring-up established the encoder polarity convention: the mirrored left
encoder is inverted in `BSP/bsp_encoder.c`, while the right encoder is not. The
BSP therefore reports positive counts for forward wheel motion on both sides.

### Ganv 8-Channel No-MCU Grayscale Sensor

The sensor uses a 74HC4051 multiplexer: one analog `OUT`, three address pins, and optional `EN`. It is not eight independent digital outputs.

| Sensor signal | MSPM0 pin | Planned role | Wiring note |
| --- | --- | --- | --- |
| `OUT` | `PA25` | ADC input | Configure as ADC/floating analog input, never GPIO output. |
| `AD0` | `PB16` | GPIO output | 4051 address bit 0. |
| `AD1` | `PB17` | GPIO output | 4051 address bit 1. |
| `AD2` | `PB18` | GPIO output | 4051 address bit 2. |
| `EN` | optional `PB19` or leave floating | GPIO output, low enabled | Manual says internal pulldown enables by default. Use PB19 only if we want firmware power gating. |
| `+5V` | stable sensor supply | Power | Manual allows 5-12 V, but use a stable clean 5 V if available. |
| `GND` | `GND` | Common ground | Do not share a noisy motor-only ground path without common reference. |
| `ERR` | not connected first pass | Optional input | Can be left floating per manual. |

4051 address order:

| AD2 | AD1 | AD0 | Selected channel |
| --- | --- | --- | --- |
| 0 | 0 | 0 | 1 |
| 0 | 0 | 1 | 2 |
| 0 | 1 | 0 | 3 |
| 0 | 1 | 1 | 4 |
| 1 | 0 | 0 | 5 |
| 1 | 0 | 1 | 6 |
| 1 | 1 | 0 | 7 |
| 1 | 1 | 1 | 8 |

### UART Allocation

| UART | MSPM0 pin | Use |
| --- | --- | --- |
| UART0 TX | `PA10` | Primary debug / wireless serial output, 115200 8N1 initially. |
| UART0 RX | `PA11` | Primary debug / wireless command input, 115200 8N1 initially. |
| UART1 TX | `PA8` | Reserved for a future external device. |
| UART1 RX | `PA9` | Reserved for a future external device. |

`PA10/PA11` are board-special pins. Do not repurpose them for GPIO, PWM, or sensors after UART0 is configured.

### ICM-45686 Module

Use I2C for the first version. This keeps the display on a separate SPI peripheral and makes IMU wiring and debugging simpler.

| Module signal | MSPM0 connection | Planned role | Wiring note |
| --- | --- | --- | --- |
| `VCC` | `3V3` | Module power | The supplied PDF is for the bare IC, not the module. Use 3.3 V until a module schematic proves that 5 V is accepted. |
| `GND` | `GND` | Common ground | Required. |
| `SCL/SCLK` | `PB2` | I2C SCL | Initial bus target: 400 kHz. |
| `SDA/MOSI` | `PB3` | I2C SDA | Confirm pull-ups are present; add external 3.3 V pull-ups if the module has none. |
| `CS` | `3V3` | Select I2C mode | The IC requires AP_CS high for I2C/I3C operation. |
| `ADD/MISO` | `GND` initially | I2C address select | Low gives address `0x68`; high gives `0x69`. |
| `INT1` | `PB24` | GPIO interrupt input | Use as the primary data-ready/FIFO interrupt. |
| `INT2` | `PB25` | GPIO interrupt input | Reserved for secondary interrupt/FSYNC use. |

Initial identity check after wiring: read `WHO_AM_I`; the ICM-45686 default value is `0xE9`.

### ST7735S 1.8-inch TFT

The supplied module schematic and initialization file identify a 128x160 ST7735/ST7735S display using 4-wire, 8-bit SPI. The module exposes no MISO, so the first driver is write-only.

| Display signal | MSPM0 connection | Planned role | Wiring note |
| --- | --- | --- | --- |
| `GND` | `GND` | Ground | Required. |
| `VDD` | `3V3` | Module power | The supplied schematic shows no obvious input regulator; use 3.3 V unless the exact module documentation states otherwise. |
| `SCL` | `PB9` | SPI1 SCLK | Start conservatively, then raise the clock after display validation. |
| `SDA` | `PB8` | SPI1 MOSI | Display write data. |
| `RST` | `PB10` | GPIO output | Active-low hardware reset. Tianmengxing LCD connector, package pin 62. |
| `DC` | `PB11` | GPIO output | Low command, high data. Tianmengxing LCD connector, package pin 63. |
| `CS` | `PB14` | GPIO output | Active low. |
| `BLK` | `PB26` | GPIO/PWM output | Backlight enable first; package pin 28 is PB26. |

The controller samples SDA on SCL rising edges. Start with SPI mode 0 and verify the first color-bar test before optimizing the bus speed.

### Reserved Future Pins

| Future module | Reserved pins | Reason |
| --- | --- | --- |
| Primary debug / wireless UART0 | `PA10` TX, `PA11` RX | Explicitly selected board UART despite the Tianmengxing special-pin caution. |
| Secondary UART1 | `PA8` TX, `PA9` RX | Reserved for future device communication. |
| ICM-45686 I2C | `PB2` SCL, `PB3` SDA, `PB24` INT1, `PB25` INT2 | Keeps IMU traffic independent from the display. |
| ST7735S SPI | `PB9` SCLK, `PB8` MOSI, `PB10` RST, `PB11` DC, `PB14` CS, `PB26` BLK | Tianmengxing LCD connector; write-only display. |
| Buttons | `PB12`, `PB13`, `PB15`, `PB23` | Four active-low GPIOs for mode/start/minus/plus. |
| Buzzer / external LED | `PB27` buzzer, `PB5` external LED | Buzzer requires a transistor/MOSFET driver. |

## Required Modules

| Module | Status | Required signals | Notes |
| --- | --- | --- | --- |
| TB6612 dual motor driver | Pin plan selected | `PWMA`, `AIN1`, `AIN2`, `PWMB`, `BIN1`, `BIN2`, `STBY`, `VM`, `VCC`, `GND` | Manual recommends 10 kHz PWM; STBY high enables. |
| Left/right DC motors with encoders | Pin plan selected | Encoder A/B per wheel, motor output through TB6612 | Still need encoder voltage and pulses per revolution. |
| Grayscale sensor array | Pin plan selected | `OUT`, `AD0`, `AD1`, `AD2`, optional `EN`, power, ground | One analog output through 74HC4051; read with ADC. |
| Buzzer / LED indicators | Pins reserved | `PB27` buzzer, `PB5` external LED, onboard `PB22` LED | Required for start, stop, and A/B/C/D point prompts. |
| Buttons | Pins reserved | `PB12`, `PB13`, `PB15`, `PB23` | Mode, start/confirm, parameter minus, parameter plus. |
| UART debug / wireless serial | Pin plan selected | UART0 `PA10/PA11`, UART1 `PA8/PA9` | Confirm the PC COM port after UART0 firmware is installed. |
| Display | Pins reserved | ST7735S write-only SPI plus reset/DC/CS/backlight | Not required for first motor bring-up. |
| ICM45686 IMU | Connected and validated | I2C1 on `PB2/PB3`; INT1/INT2 wired on `PB24/PB25` | `WHO_AM_I=0xE9`, 200 Hz data and relative yaw validated. Interrupts are not needed by the first polling driver. |

## Pin Assignment Rules

- Prefer SysConfig-assigned pins with no board caveat.
- Keep motor PWM pins on timer CCP outputs.
- Keep encoder pins on timer capture or interrupt-capable GPIO when practical.
- Keep grayscale analog outputs on ADC-capable pins if the sensor is analog.
- Keep UART pins compatible with the actual serial adapter and cross TX/RX.
- After each pin decision, update this table and regenerate SysConfig before writing C code against generated names.

## Initial Wiring Checklist

- Common ground between MCU, TB6612 logic, battery power system, sensors, and serial adapter.
- TB6612 logic `VCC` matches MCU logic level.
- TB6612 motor `VM` comes from the motor battery path, not from MCU 3.3 V.
- Motors are first tested with wheels lifted.
- Encoder and sensor outputs never exceed MSPM0 input limits.
- Any external I2C modules have correct pull-ups for the selected voltage.
