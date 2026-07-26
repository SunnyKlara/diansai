# Serial Debug Protocol Draft

Primary debug target: UART0 on `PA10` TX / `PA11` RX, 115200 baud, 8 data bits, no parity, 1 stop bit.

UART1 on `PA8` TX / `PA9` RX is reserved for future device communication. Add both peripherals in `empty.syscfg` only when their corresponding bring-up stage starts.

## Command Set

Commands are ASCII line based and end with `\n`.

| Command | Purpose |
| --- | --- |
| `ping` | Reply with firmware heartbeat/version. |
| `stop` | Immediately command zero motor output and enter idle/safe state. |
| `start` | Start the selected route after the car is placed at A. |
| `mode ab` | Select requirement 1: A->B and stop. |
| `mode loop_abcd` | Select requirement 2: A->B->C->D->A. |
| `mode loop_acbd` | Select requirement 3/4: A->C->B->D->A. |
| `speed <value>` | Set base speed for line following. |
| `kp <value>` | Set line PID proportional gain. |
| `ki <value>` | Set line PID integral gain. |
| `kd <value>` | Set line PID derivative gain. |
| `log on` / `log off` | Enable or disable periodic telemetry. |

Bring-up-only commands currently implemented:

| Command | Purpose |
| --- | --- |
| `gray` | Print filtered ADC values for all eight channels. |
| `cal start` | Start an 8-second RAM calibration; manually sweep the black line across every sensor. Motors are stopped first. |
| `cal stop` | End calibration early and print per-channel minima/maxima. |
| `cal` | Print calibration state, valid-channel count, minima, and maxima. |
| `line` | Print normalized blackness, weighted line error, strength, and lost-line flag. |
| `imu` | Print ICM-45686 state, acceleration, angular rate, gyro bias, relative yaw, and I2C error count. |
| `imu cal` | Restart the two-second stationary gyroscope bias calibration. Keep the car still. |
| `yaw zero` | Set the current vehicle heading to zero without recalibrating gyro bias. |
| `route3 ac` | A-to-C bring-up: zero yaw, hold the manually aimed heading, and stop when the C arc is detected after the minimum distance. |
| `route3 acb` | Run A-to-C continuously into the C-to-B semicircle, then stop when the line ends at B. |
| `route3 cb` | C-to-B bring-up: from the A-to-C diagonal heading, make a forward 38.7-degree turn, acquire the right arc, follow it, and stop at B. |
| `route3 full` | Run one complete requirement-3 lap: A-to-C-to-B-to-D-to-A. |
| `motor <left> <right>` | Apply bounded permille commands for one second, then stop automatically. |
| `speed <left> <right>` | Run independent wheel-speed PI loops at signed counts per 10 ms for three seconds. Targets are limited to +/-30. |
| `pi` | Print wheel-speed PI gains. |
| `pi <Kp100> <Ki100> <FF100>` | Set fixed-point speed gains; defaults `300 10 850` mean Kp 3.00, Ki 0.10 per 10 ms, feedforward 8.50 PWM/count. |
| `follow <8..20>` | Follow the calibrated line for two seconds using line PD plus wheel-speed PI; persistent line loss stops immediately. |
| `followrun <8..20>` | Follow until 50 ms persistent line loss, with a 15-second guard timeout. Intended for complete arc/segment tests. |
| `followarc <8..20> <-6..6>` | Follow a known-radius arc with signed differential-speed feedforward plus encoder yaw damping. The currently tested arc uses bias `-2`; the opposite direction uses `+2`. |
| `followfine <8..20> <-6..6>` | Validated continuous grayscale/encoder arc control with 0.01 count/10 ms wheel-speed targets and IMU logging only. |
| `lpd` | Print line-steering PD gains. |
| `lpd <Kp10000> <Kd10000>` | Set line PD gains; defaults `40 220` mean Kp 0.0040 and Kd 0.0220. |

## Telemetry Fields

Recommended compact log line:

```text
t=<ms>,state=<id>,mode=<id>,line=<err>,lost=<0|1>,vl=<left>,vr=<right>,pl=<pwm>,pr=<pwm>,event=<id>
```

Use telemetry for real motion tests before using debugger halt/breakpoints.

## Temporary PB21 Demo

For the current route-3 placement tests, pressing onboard `PB21` while idle
starts `route3 full`: current heading is zeroed and the car runs one complete
A-to-C-to-B-to-D-to-A lap. Pressing PB21 again while running is an immediate
manual stop. This mapping is temporary.

During each semicircle, line loss is accepted as an endpoint only after at
least 5000 average encoder counts and when IMU yaw is within 35 degrees of the
expected B or A exit heading. The endpoint then requires 12 consecutive lost
samples (120 ms). Before it is confirmed, line loss holds the latest filtered
line error instead of advancing the route state. Route telemetry reports this
as `endarm=0/1` and `endlost=0..12`.

Route point transitions use a stopped pivot turn: the wheels run in opposite
directions until yaw is within 3.5 degrees of the target and angular rate has
settled for five 10 ms samples. C and D then move forward at low speed to
reacquire the arc; B starts the B-to-D diagonal after the pivot completes.
