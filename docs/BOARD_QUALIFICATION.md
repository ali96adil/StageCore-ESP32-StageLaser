# StageLaser physical board qualification

## Qualification source

The actual ESP32-C3 Super Mini intended for StageLaser was connected over its native USB-C port and queried with **esptool 5.2.0**.

No firmware was written during this identification step.

## Verified facts

| Property | Verified value |
| --- | --- |
| Chip | ESP32-C3 |
| Package | QFN32 |
| Silicon revision | v0.4 |
| CPU | Single-core RISC-V, 160 MHz |
| Flash | Embedded 4 MB, XMC |
| Flash manufacturer | 46 |
| Flash device | 4016 |
| Crystal | 40 MHz |
| USB mode | USB-Serial/JTAG |
| Secure Boot | Disabled |
| Flash Encryption | Disabled |
| SPI boot crypt count | 0 |

The unique device MAC is intentionally omitted from this public document.

## What this qualification changes

These items are no longer assumptions:
- ESP32-C3 family
- silicon/package revision
- 4 MB physical flash capacity
- 40 MHz crystal
- native USB-Serial/JTAG availability

The existing 4 MB CI geometry is therefore capacity-compatible with the real board.

## What this does NOT qualify

This checkpoint does **not** authorize relay actuation.

Still pending:
- safe relay output GPIO
- output idle polarity
- relay-module input voltage behavior
- direct 3.3 V compatibility vs transistor/MOSFET interface
- boot/reset electrical idle behavior
- shared-power qualification
- production OTA partition layout
- physical recovery input

The firmware must remain:
```text
STAGECORE_LASER_ACTUATION_ENABLED=0
STAGECORE_LASER_OUTPUT_GPIO=-1
STAGECORE_LASER_SHARED_POWER_QUALIFIED=0
```

until relay/electrical qualification passes.
