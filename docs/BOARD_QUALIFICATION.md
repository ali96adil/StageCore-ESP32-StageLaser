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

The firmware console is explicitly configured for the verified native
USB-Serial/JTAG controller using `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, with
the secondary console disabled. This keeps first-boot logs on the same USB-C
interface used for flashing and avoids depending on UART0 GPIO20/GPIO21.

## Relay electrical observations

Measured on the actual 5 V relay module:
- control input idles at approximately 5 V when left open
- pulling IN to GND actuates the relay
- control input is therefore treated as **active-low**
- direct connection from relay IN to an ESP32-C3 GPIO is prohibited because the relay input side exposes approximately 5 V
- the production interface must use a qualified open-collector/open-drain stage (for example an NPN transistor or suitable MOSFET/level interface) so the ESP32 only requests a pull-to-ground

## GPIO candidate review

The first no-load candidate is **GPIO3**.

Rationale from the ESP32-C3 hardware documentation:
- GPIO3 is not a strapping pin
- GPIO3 is not a USB Serial/JTAG pin
- GPIO3 is not part of the SPI0/1 flash range
- GPIO3 is not listed among the documented power-up glitch pins
- the pin is input-enabled after reset, so the external interface must hold the relay driver OFF until firmware explicitly configures the output

Pins deliberately excluded from first-choice relay drive:
- GPIO2 / GPIO8 / GPIO9: strapping
- GPIO18 / GPIO19: native USB Serial/JTAG
- GPIO12..GPIO17: flash/SPI0/1
- GPIO20 / GPIO21: UART0 defaults
- GPIO6 / GPIO7 / GPIO10: documented power-up glitch behavior makes them poor choices for a no-unintended-pulse safety output

GPIO3 is a **candidate only**, not yet qualified. The physical no-load boot/reset/bootloader test must pass before it can become the StageLaser output GPIO.

## What this does NOT qualify

This checkpoint does **not** authorize relay actuation.

Still pending:
- physical no-load qualification of GPIO3
- transistor/MOSFET interface qualification
- boot/reset electrical idle proof
- relay-only pulse/rest qualification
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
