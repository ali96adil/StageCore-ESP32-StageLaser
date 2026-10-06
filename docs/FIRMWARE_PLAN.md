# StageLaser firmware bootstrap

This bootstrap intentionally stops before hardware actuation.

## Current implemented slice

- canonical StageLaser contract constants
- pure C++ desired-state state machine
- DISARMED boot
- ON/OFF idempotency
- state commit only after confirmed contact release
- interrupted transition -> UNKNOWN
- UNKNOWN Safe Off -> fail closed / no pulse
- bounded local Flash timing model
- mechanical-relay default limits
- output-driver abstraction
- compile-time NO-ACTUATION hardware gate
- host tests and CI skeleton

## Next firmware slices after repository creation

1. persistent P-256 device identity
2. Wi-Fi provisioning + fallback AP/settings page
3. `_stagecore-hub._tcp` discovery
4. Hub identity + TLS certificate pinning
5. pairing/authentication
6. `stagecore.device/2` WebSocket runtime
7. StageLaser assignment safe-off prepare/ACK and scope ACK
8. command envelope validation + bounded command-ID result journal
9. NVS stable-state + interrupted-transition persistence
10. observation/heartbeat payload
11. local OTA/recovery page
12. hardware-qualified relay driver

Reuse/adapt the non-DMX infrastructure from `StageCore-ESP32-DMX-Lighting`; do not copy DMX-specific output code.

## Hardware gate

Do not set `STAGECORE_LASER_ACTUATION_ENABLED=1` until the exact ESP32-C3 Super Mini and relay module are physically identified and tested. The final environment must document GPIO, relay polarity, boot/reset inactive level, 3.3 V compatibility, and contact wiring.
