# StageCore ESP32 StageLaser

Production firmware for the official StageCore **StageLaser** device.

The target is an **ESP32-C3 Super Mini** controlling a **1-channel dry-contact relay/electronic switch** that electrically simulates the laser's original momentary toggle button. StageCore never exposes a raw toggle command: the public API is desired-state ON/OFF plus ARM/DISARM, local Flash, Safe Off and attended state resync.

## Software checkpoint

StageLaser Core integration is merged in `ali96adil/StageCore` via PR #428.

The firmware software stack is complete through the authenticated Stage Device v2 command boundary:

- deterministic desired-state state machine
- DISARMED boot and ARM-before-ON/Flash
- idempotent SET ON/OFF
- 180 ms default pulse and 250 ms minimum rest
- bounded local Flash timing
- stable state committed only after contact release
- interrupted pulse / restart-during-Flash -> UNKNOWN
- reset-class-aware state restoration
- NVS physical-truth persistence
- persist-before-PICK transaction controller
- persistent P-256 device identity
- Wi-Fi provisioning and reconnect policy
- StageCore Hub mDNS discovery
- TLS 1.3 certificate pinning
- pairing/authentication
- native `stagecore.device/2` TLS WebSocket runtime
- authenticated safe assignment
- Hub-owned ACTIVE scope ACK / `runtime.ready` fencing
- strict command envelope with exact Project + Runtime Snapshot authority
- deadline validation using Hub-trusted time
- bounded same-session command-result journal
- persistent 32-entry SHA-256 replay fence for commands that can actuate
- local Flash scheduler; no network-streamed toggles
- connection-loss deterministic Safe Off
- StageLaser observations for arm/logical state, quality, RSSI, uptime, pulse count and command diagnostics

The default build remains deliberately **NO-ACTUATION** until the exact ESP32-C3 Super Mini and relay module are physically qualified.

## Safety invariants

- no `LASER_TOGGLE`
- boot never creates a relay pulse
- ARM is required before ON or Flash
- SET ON/OFF are desired-state and idempotent
- logical ON/OFF changes only after relay release completes
- UNKNOWN never triggers a blind pulse
- DISARM / Safe Off attempt OFF only when state is known
- Flash timing runs locally on the ESP32
- Hub loss removes command authority and attempts deterministic Safe Off
- controller persistence or relay faults end runtime authority
- Project/Snapshot authority is never persisted as physical truth
- an actuation-capable `command_id` is durably fenced before controller execution
- replay of that `command_id` after reboot cannot create a second pulse
- default CI firmware cannot drive a relay GPIO

## CI safety target

PlatformIO builds a generic ESP32-C3 image with:

```text
STAGECORE_LASER_ACTUATION_ENABLED=0
STAGECORE_LASER_OUTPUT_GPIO=-1
STAGECORE_LASER_SHARED_POWER_QUALIFIED=0
```

The current 4 MB partition table is **CI-only**. It is not a claim about the real Super Mini flash size or production OTA geometry.

## Hardware gate

Do not enable physical actuation until the exact board and relay module are inspected and tested.

Qualification must establish:

1. exact ESP32-C3 Super Mini revision and flash size
2. safe output GPIO
3. relay/input polarity
4. 3.3 V logic compatibility
5. boot/reset GPIO level never closes the contact
6. dry-contact wiring across the original laser button
7. ESP32 vs laser power-domain relationship
8. real cold-power behavior: laser physically starts OFF
9. pulse-width and minimum-rest behavior on the real relay
10. production flash / OTA partition geometry
11. a safe physical local-recovery input if one is added

Only after that gate may a hardware environment set actuation enabled.

See `docs/FIRMWARE_PLAN.md` for the acceptance sequence.
