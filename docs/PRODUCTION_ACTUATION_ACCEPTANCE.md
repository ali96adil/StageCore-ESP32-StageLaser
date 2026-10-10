# StageLaser physical actuation release gate

Status: **SOURCE IMPLEMENTED; NOT YET PHYSICALLY QUALIFIED FOR LASER EMISSION**.
This guide is not a grant of physical safety certification. Keep the existing default
NO-ACTUATION image until all gates below are evidenced.

## Actual control interface

The ESP32-C3 drives the qualified NPN transistor interface, **not** the
5 V active-low relay input directly:

- ESP32-C3 GPIO3 LOW or high-impedance: NPN OFF, relay contact RELEASED.
- ESP32-C3 GPIO3 HIGH: NPN ON, relay IN pulled to ground, contact PICKED.
- The user's relay contact emulates a momentary **toggle button**, not a
  maintained laser-off / beam-inhibit control.
- The physical laser can remain ON when the ESP32 alone loses power if its
  independent power domain remains energized. Software cannot prevent that.

## Operator-confirmed installed controls (2026-10-10)

- **Main power isolator:** disconnects the supply to **both ESP32 and laser**.
- **Laser control:** a **momentary toggle pushbutton**; each press changes
  the laser's state, rather than holding power OFF while the control is open.
- **Consequences:** the main isolator is an effective deliberate all-power
  shutdown when opened, but it does not automatically detect an ESP32-only
  crash or loss of ESP32 power. The momentary toggle contact cannot guarantee
  beam OFF by merely releasing GPIO3. Neither control, as described, proves
  an independent automatic beam inhibit on controller failure.
- The GPIO3 active-HIGH NPN pulse driver remains implemented but **not
  authorized for show actuation** by this wiring confirmation alone.

## Non-negotiable hardware proof before a show-use actuation build

1. Verify the **specific installed** driver hardware (NPN polarity, base bias,
   5 V isolation, GPIO3 reset/boot/USB flashing transitions, no HIGH spike).
2. Verify **independent automatic beam inhibit** / appropriate emission interlock
   (or manufacturer-approved equivalent) while ESP32 is unpowered or crashed,
   during network/Hub loss, and during emergency stop. A manual master power
   switch is valuable but does not by itself meet this automatic-fault test.
3. Confirm physically measured **laser emission OFF**, not only a status LED,
   during all failure modes; provide a test record and model/laser interlock
   details without secrets or personal identifiers.
4. Validate the whole-circuit cold boot OFF under the exact master-power
   configuration; validate separately toggling power to ESP32 only.
5. With the laser **disconnected**, test relay single PICK/RELEASE,
   180 ms nominal pulse, >=250 ms rest, restart during pulse, and watchdog.
6. Confirm the current project/snapshot scope, replay fence, resync behavior,
   Emergency Blackout strategy and manual rescue path under rehearsal conditions.
7. Pin and archive exact firmware SHA, OTA/bootloader partition layout, backup,
   power wiring, relay qualified pin, and StageCore Hub SHA.

The operator has reported confidence in laser protection, earlier cold-power
OFF, successful relay bench tests, and use of a manually operated whole-circuit
master switch. These reports are meaningful but do not establish item 2. The
previously observed ESP-only shutdown leaving an energized laser unchanged is
a separate failure mode that the production architecture must handle.

## Firmware build fences

Default CI images **cannot actuate**. The GPIO driver implementation is
source-complete behind all of these explicit compile-time requirements:

- `STAGECORE_LASER_ACTUATION_ENABLED=1`
- `STAGECORE_LASER_OUTPUT_GPIO=3`
- `STAGECORE_LASER_RELAY_DRIVER_QUALIFIED=1`
- `STAGECORE_LASER_INDEPENDENT_INTERLOCK_QUALIFIED=1`
- `STAGECORE_LASER_SHARED_POWER_QUALIFIED=1`

These defines are *human-provided physical qualification declarations*, not
runtime sensor data and not proof of safety. Don't enable them until the
corresponding tests have actually passed on the installed rig.

No production actuation environment, flash procedure, or actuation binary is
provided by this source PR. An attended physically reviewed release procedure
must be prepared separately after the hardware gate. Do not hand an unqualified
actuation binary to a show operator.

## Code/CI gates

- No simulated GPIO pulse tests are required for this operator workflow;
  real relay behavior is qualified on the installed GPIO3 circuit with laser
  emission disconnected before production use.
- Standard CI builds only no-actuation, GPIO3 no-load, and OTA
  no-actuation environments.
- A passing CI checks firmware *source*, not laser emission or interlock
  behavior.
