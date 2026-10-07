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
- persistent validated relay/output timing limits
- OTA candidate safe-boot rollback confirmation after local persistence/configuration restore
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

The default build remains deliberately **NO-ACTUATION**. The physical ESP32-C3 Super Mini board identity and flash geometry have now been verified; relay/GPIO/electrical qualification is still pending.

## Safety invariants

- no raw toggle command or API
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

The physical StageLaser board has been verified with esptool as ESP32-C3 QFN32 rev v0.4 with embedded XMC 4 MB flash and a 40 MHz crystal. The default partition table is still **CI-only**. The separate 4 MB dual-slot OTA candidate is structurally CI-validated and uses application rollback, but OTA transport plus physical update/rollback behavior are not qualified yet.

## Hardware gate

Do not enable physical actuation until the exact board and relay module are inspected and tested.

Verified board facts:
- ESP32-C3 QFN32 revision v0.4
- embedded XMC 4 MB flash
- 40 MHz crystal
- native USB-Serial/JTAG
- Secure Boot disabled on the qualification unit
- Flash Encryption disabled on the qualification unit

Qualification still must establish:

1. safe output GPIO
2. relay/input polarity
3. 3.3 V logic compatibility
4. boot/reset GPIO level never closes the contact
5. dry-contact wiring across the original laser button
6. ESP32 vs laser power-domain relationship
7. real cold-power behavior: laser physically starts OFF
8. pulse-width and minimum-rest behavior on the real relay
9. production flash / OTA partition geometry
10. a safe physical local-recovery input if one is added

Only after that gate may a hardware environment set actuation enabled.

See `docs/FIRMWARE_PLAN.md` for the acceptance sequence.

## First hardware flash

The first ESP32-C3 flash must use the NO-ACTUATION environment with the relay and laser disconnected. Follow `docs/FIRST_BOOT_QUALIFICATION.md` and validate the captured serial log with `tools/check_first_boot_log.py` before starting GPIO or relay qualification.
