# StageLamp GPIO3 live relay profile — StageLaser legacy device name

The actual fixture is **an ordinary visible stage lamp**, not a laser emitter.
Keep the existing StageLaser name and stagecore.esp32-stagelaser protocol so
already-authored StageCore Cues, assignments and published snapshot bindings
are not silently broken.

## Control

- ESP32-C3 GPIO3 -> qualified NPN transistor -> 5 V active-low relay IN.
- GPIO3 LOW/Hi-Z releases the relay; HIGH holds the relay only during the
  bounded 180 ms pushbutton pulse. Never drive a 5 V relay input from GPIO3.
- The NO/COM dry contact is parallel to the fixture's momentary toggle button.
  Each press changes the lamp's own internal ON/OFF state.
- A controller restart or network loss **does not by itself switch the lamp
  OFF**; releasing the momentary contact is not a maintained power disconnect.
  Use the independently wired main power isolator to make the fixture dark.
- StageCore tracks state based on successful pulse/release, not a light sensor.
  If power was interrupted independently or the displayed state disagrees with
  reality, use the existing attended **State Resync** in device settings.
- A cold boot can be treated as OFF only when power to the fixture and ESP32
  was truly removed together and this particular fixture reliably starts OFF.
  The explicit build flag reflects the operator's reported wiring; it does
  not measure power-sharing on its own.

## Build

Default `esp32c3-ci-no-actuation` remains non-actuating. For this ordinary
lamp and **not** a genuine laser or hazardous output:

```bash
cd ~/StageCore-ESP32-StageLaser
git fetch origin
git switch release/stage-lamp-gpio3-20261010
git pull --ff-only
source ~/.venv-pio/bin/activate
pio run -e esp32c3-stage-lamp-gpio3-live
```

Before USB flashing, the operator must verify that GPIO3 drives the NPN stage,
that GPIO3 LOW releases the relay, that GPIO3 HIGH momentarily closes the
contact, that no startup pulse occurs, and that the manual power disconnect
really turns the lamp OFF. Match the connected ESP32-C3 serial port, then
flash **only while the lamp output is isolated for the first power-up**.
Use the project's standard PlatformIO upload workflow with the selected
environment and serial port.

## Hub compatibility

This branch descends from firmware PR #56 and expects the
`control_generation` extension from StageCore Hub draft PR #475.
**Do not flash it on a show setup backed by an older Hub**, which can reject
the mismatched commands. The Hub and firmware must be version-paired,
backed up, and checked with the exact published runtime snapshot.

The lamp's software SAFE_OFF uses a second toggle pulse only if its tracked
state is confidently ON. On UNKNOWN, it refuses a blind toggle. For a
guaranteed dark state regardless of controller condition, use the real main
power disconnect. STOP CUE is not an all-fixture power disconnect.

## Limitations

Firmware CI proves source and binary compilation. It cannot verify
the attached transistor, relay, fixture, mains wiring, physical state, or
four-tablet/DMX/show Cue behavior. Any external mains supply must remain
wired and protected by qualified electrical hardware; the ESP GPIO circuit
must remain in the isolated low-voltage control path.
