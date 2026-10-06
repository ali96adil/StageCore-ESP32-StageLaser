# GPIO3 no-load qualification target

This target exists only to qualify the first relay-output candidate on the
actual ESP32-C3 Super Mini.

## Safety boundary

Use it only with:

- relay disconnected
- laser disconnected
- multimeter or logic analyzer on GPIO3 relative to GND

The target is **not** an actuation firmware. It inherits the normal StageLaser
NO-ACTUATION environment:

```text
STAGECORE_LASER_ACTUATION_ENABLED=0
STAGECORE_LASER_OUTPUT_GPIO=-1
STAGECORE_LASER_SHARED_POWER_QUALIFIED=0
```

It adds only:

```text
STAGECORE_GPIO_NO_LOAD_QUALIFICATION=1
STAGECORE_GPIO_NO_LOAD_QUALIFICATION_GPIO=3
```

After application startup, GPIO3 is held LOW continuously. The qualification
code never requests HIGH.

For the proposed NPN driver, LOW means transistor OFF / relay released.

## Why this separate target exists

The ordinary NO-ACTUATION image intentionally never touches a relay GPIO, so it
cannot prove the application's GPIO3 configuration behavior.

This target lets the attended bench test observe:

- cold-power behavior before application code runs
- reset behavior
- BOOT + RESET / download-mode behavior
- transition from ROM/bootloader state into an application-held LOW
- any unwanted HIGH excursion

## Build

```bash
pio run -e esp32c3-gpio3-no-load-qualification
```

Do not upload this target until the normal first-boot NO-ACTUATION
qualification has already passed.

## Physical acceptance

With relay and laser disconnected:

1. monitor GPIO3 to GND
2. cold-power the board
3. press/release RESET
4. enter BOOT + RESET download mode
5. return to normal boot
6. repeat each transition at least 10 times

Acceptance requires no unsafe HIGH excursion that could turn on the proposed
NPN stage.

A multimeter is acceptable for coarse checks. A logic analyzer or oscilloscope
is preferred for short boot transients.

Failure means GPIO3 is rejected and must not be promoted into a StageLaser
hardware environment.
