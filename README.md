# StageCore ESP32 StageLaser — bootstrap

Production firmware for the StageCore StageLaser device will live in `ali96adil/StageCore-ESP32-StageLaser`.

This bootstrap is deliberately **NO-ACTUATION**. It implements the safety-critical pure state-machine foundation and CI structure without selecting or driving a physical relay GPIO before the real ESP32-C3 Super Mini and relay module are qualified.

## Safety invariants

- no raw toggle API
- ARM required before ON/Flash
- SET ON/OFF are desired-state and idempotent
- logical state changes only after contact release is confirmed
- interrupted transition boots UNKNOWN
- UNKNOWN Safe Off never guesses with a pulse
- bounded local Flash
- boot DISARMED
- default build cannot actuate hardware

## Host tests

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  src/laser_state_machine.cpp tests/state_machine_test.cpp \
  -o /tmp/stagelaser-state-machine-test
/tmp/stagelaser-state-machine-test
python -m unittest discover -s tests -p '*_test.py' -v
```

See `docs/FIRMWARE_PLAN.md` for the next slices.
