# StageLaser: relay-only manually gated qualification

**This image is not a show/production firmware. It intentionally cannot join
StageCore and cannot operate a laser safely.**

Use only on an attended bench after independently confirming that the NPN
interface presents the correct GPIO polarity, 3.3 V isolation, external bias,
and that a reset does not close the relay. A visual/audible check alone is
not enough to rule out short transients.

## Hard prerequisites

- Physically disconnect both leads to the actual laser from COM/NO/NC.
- Keep laser power OFF and out of the test area.
- Keep the relay supply correctly limited and the ESP32 powered normally.
- Confirm that GPIO3 LOW releases the relay, GPIO3 HIGH causes transistor
  sinking of the 5 V active-low relay input (never connect 5 V to GPIO3).
- Confirm external fail-off bias during boot/reset; use a scope/logic
  analyzer where possible to establish no unintended contact closures.
- Have someone watching the relay, and be ready to remove power.

## Opt-in firmware

The only bench environment is:

`esp32c3-relay-only-manual-bench`

It retains `STAGECORE_LASER_ACTUATION_ENABLED=0` and
`STAGECORE_LASER_OUTPUT_GPIO=-1` and adds the independent
`STAGECORE_RELAY_BENCH_ONLY=1` mode.

The regular CI and GPIO3 no-load images are unchanged.

```bash
pio run -e esp32c3-relay-only-manual-bench
pio run -e esp32c3-relay-only-manual-bench -t upload --upload-port /dev/cu.usbmodem101
pio device monitor -p /dev/cu.usbmodem101 -b 115200
```

Expected idle messages include (repeated after a 2-second USB reconnect settle period):

```text
RELAY-ONLY BENCH MODE: NO LASER CONNECTED TO COM/NO/NC
GPIO3 LOW; relay must be released
STATUS = read-only status; PULSE = one manual 180 ms pulse
To issue ONE manual 180 ms pulse, type PULSE and press Enter
```

1. Boot and reset repeatedly, verify relay stays inactive.
   If USB reconnects after the initial banner, wait for the delayed banner.
   Type exact uppercase `STATUS` followed by Enter to check read-only
   state; expect `GPIO3 LOW, pulse_used=no` without relay motion.
2. With the laser disconnected, type exact uppercase `PULSE` then Enter
   in the USB serial console. Only one HIGH pulse of 180 ms is attempted.
3. Watch for relay activation and release; verify COM/NO closure using
   suitable instrumentation. Clicking only is not an electrical timing
   measurement.
4. Repeating `PULSE` in the same boot must be rejected.
5. Lose Wi-Fi / unplug Ethernet; the bench firmware has no network.
6. Restore the approved `esp32c3-ci-no-actuation` firmware if stopping tests.

Do not treat successful bench pulses as proof that laser ON/OFF state is known
across cold power, runtime restart, Wi-Fi loss or the initial physical-button
power-on behavior. All of those are independent production acceptance gates.
