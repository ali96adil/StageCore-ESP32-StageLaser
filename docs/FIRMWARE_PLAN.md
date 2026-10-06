# StageLaser firmware qualification checkpoint

## Software-complete boundary

StageLaser V1 Core is merged in StageCore, and the dedicated ESP32-C3 firmware has reached the authenticated command-runtime boundary while remaining NO-ACTUATION.

Implemented software layers:

1. desired-state safety contract and state machine
2. transaction controller with persist-before-PICK / commit-after-RELEASE
3. NVS truth-state persistence and reset classification
4. persistent P-256 identity
5. Wi-Fi provisioning and reconnect
6. `_stagecore-hub._tcp` discovery
7. Hub identity and TLS certificate pinning
8. pairing / challenge authentication
9. `stagecore.device/2` TLS WebSocket runtime
10. authenticated StageLaser assignment safe-off ACK
11. ACTIVE scope ACK + Hub `runtime.ready` command fence
12. strict command envelope, scope and deadline validation
13. ARM / DISARM / SET ON / SET OFF / Flash / Safe Off / Read / Resync routing
14. local bounded Flash scheduler
15. observations and command diagnostics
16. connection-loss deterministic Safe Off
17. bounded same-session command result dedupe
18. persistent 32-entry SHA-256 actuation replay fence written before controller execution

## Deliberately not qualified yet

These require the real hardware and must not be guessed:

- physical relay GPIO
- relay active polarity
- relay module 3.3 V electrical compatibility
- boot/reset inactive electrical behavior
- shared-power qualification
- actual flash size and production partition map
- OTA layout
- physical local-recovery button GPIO

The current firmware build remains NO-ACTUATION.

## Hardware acceptance sequence

Run these in order after the exact ESP32-C3 Super Mini and relay module are available.

### 1. Visual and electrical identification
- photograph both sides of the ESP32-C3 Super Mini
- photograph relay board front/back and all markings
- identify VCC / GND / IN and COM / NO / NC
- confirm relay input voltage/current and isolation/transistor topology

### 2. No-load GPIO qualification
- choose a GPIO with no unsafe strap/boot behavior
- confirm power-up, reset and bootloader entry never assert the output
- measure idle level with a multimeter or logic analyzer

### 3. Relay-only qualification
- keep the laser disconnected
- verify PICK / RELEASE polarity
- validate 180 ms pulse
- validate 250 ms minimum rest
- repeat boot/reset tests and prove zero unintended contact pulses

### 4. Laser button wiring
- connect only the relay dry contact across the original button contacts
- use COM/NO unless the measured button circuit proves otherwise
- preserve the physical button where practical
- prove one electrical pulse equals exactly one physical toggle

### 5. Cold-power qualification
- power-cycle laser + StageLaser together and separately
- verify the physical laser starts OFF after true cold power-up
- prove whether shared-power qualification is valid
- if not proven, keep `STAGECORE_LASER_SHARED_POWER_QUALIFIED=0`

### 6. Desired-state acceptance
- cold boot: zero pulse
- ARM: zero pulse
- ON: exactly one pulse
- ON again: zero additional pulses
- OFF: exactly one pulse
- OFF again: zero additional pulses

### 7. Flash acceptance
- Flash Start at supported frequency/duration
- Flash timing remains local if network traffic pauses
- Flash Stop settles to OFF
- natural expiry settles to OFF
- network loss settles to deterministic OFF when state is known

### 8. Failure, reset and replay acceptance
- same command_id in one session: no second pulse
- same actuation command_id after reboot: no second pulse
- reset during pulse: reboot UNKNOWN
- reset between Flash phases: reboot UNKNOWN
- NVS failure: no actuation
- relay PICK/RELEASE failure: UNKNOWN and runtime authority ends
- ESP-only restart while laser remains powered never assumes OFF

### 9. StageCore integration
- pairing and Devices card
- safe assignment to a published Runtime Snapshot
- ARM / ON / OFF from Operator
- Cue Builder StageLaser action
- Mixed Cue
- Flash Start / Stop
- managed Emergency Blackout / Safe Off
- snapshot republish/reconnect
- Hub restart and Wi-Fi reconnect
- attended resync flow

### 10. Production update and recovery path
- confirm real flash size
- define OTA partitions
- qualify the controlled OTA path
- choose/qualify a physical local-recovery/trust-reset input if required

## Release gate

A hardware-actuation build may be created only after steps 1–8 pass.

StageCore show-use qualification requires steps 1–10.
