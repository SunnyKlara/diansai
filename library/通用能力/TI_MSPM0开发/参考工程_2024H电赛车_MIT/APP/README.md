# APP Ownership

`APP` owns behavior that is independent of physical pin names and DriverLib details.

Planned modules:

- `app_car.c/.h`: top-level initialization and cooperative scheduling.
- `app_control.c/.h`: line PID, wheel-speed PID, limits, and lost-line recovery.
- `app_speed_control.c/.h`: active 10 ms left/right wheel-speed PI controller, target ramp, feedforward, and output limits.
- `app_line_control.c/.h`: line-error PD steering that commands differential wheel-speed targets and stops after persistent line loss.
- `app_attitude.c/.h`: stationary gyro bias calibration and relative Z-axis yaw integration.
- `app_heading_control.c/.h`: IMU yaw PD steering for unmarked straight segments.
- `app_route3.c/.h`: requirement-3 ACBD route state machine, initially bringing up the A-to-C segment.
- `app_route.c/.h`: A/B/C/D route state machine, lap counting, prompts, and stopping rules.
- `app_commands.c/.h`: serial command parser and parameter updates.
- `app_ui.c/.h`: button/menu/display presentation logic.
- `app_config.h`: tunable defaults and route-independent limits.

The root `empty.c` remains the normal main entrypoint. It should initialize SysConfig, initialize BSP and APP modules, dispatch pending work, and sleep when idle. It should not contain driver implementations or route logic.
