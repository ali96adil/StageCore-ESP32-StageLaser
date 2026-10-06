# StageLaser relay-driver interface plan

This document defines the **bench candidate** interface between the ESP32-C3 and
the measured 5 V active-low relay module.

It does **not** authorize StageLaser actuation. The production firmware remains
NO-ACTUATION until the physical interface and boot/reset behavior pass the
hardware gate.

## Measured relay behavior

On the actual relay module:

- VCC: 5 V
- IN idles at approximately 5 V
- pulling IN to GND actuates the relay
- therefore the module is treated as active-low
- relay IN must not be wired directly to an ESP32-C3 GPIO

The ESP32-C3 side is a 3.3 V logic domain.

## Preferred V1 driver

Use an NPN transistor as an open-collector pull-to-ground stage.

Candidate parts:

- 2N2222 / PN2222 family
- BC547 family

Exact E/B/C pin order varies by package and manufacturer. Verify the purchased
part's datasheet or meter-test the transistor before wiring.

### Bench wiring

```text
ESP32-C3 GPIO3 ---- 4.7 kΩ ---- Base
                                |
                              100 kΩ
                                |
ESP32 GND ----------------------+- Emitter
                                   NPN
Relay IN -------------------------- Collector

Relay VCC ------------------------- +5 V
Relay GND ------------------------- ESP32 GND
```

The 100 kΩ base-to-emitter resistor is a hardware default-OFF bias. It must keep
the transistor off while the ESP32 pin is high-impedance during reset/boot.

Expected logic with this NPN interface:

- ESP GPIO LOW / high-impedance -> transistor OFF -> relay IN remains high -> relay RELEASED
- ESP GPIO HIGH -> transistor ON -> relay IN pulled to GND -> relay PICKED

This means the **relay module input is active-low**, while the **ESP-side NPN
driver request is active-high**. Keep those two polarities distinct in firmware
and documentation.

## Alternative level-shifter module

A common BSS138 bidirectional I2C logic-level converter may be useful for a
bench experiment because it can translate an open-drain pull-to-ground signal,
but it is **not the production default** until the exact module topology,
pull-ups and boot behavior are inspected.

Do not assume every product sold as an I2C level converter has the same circuit.

## GPIO candidate

GPIO3 is the first no-load candidate.

It remains unqualified until the actual board passes the following test with
**nothing connected to GPIO3**.

## No-load GPIO3 acceptance

1. Flash the normal NO-ACTUATION StageLaser image first and prove normal boot.
2. Keep relay and laser completely disconnected.
3. Measure GPIO3 relative to GND during:
   - USB insertion / cold power-up
   - RESET press/release
   - BOOT + RESET download-mode entry
   - normal application startup
4. There must be no unsafe HIGH pulse capable of turning the proposed NPN stage on.
5. Repeat each transition at least 10 times.
6. If a logic analyzer is available, use it in addition to the multimeter.

GPIO3 must not be promoted to the StageLaser hardware profile until this passes.

## Relay-only acceptance after GPIO3 passes

Keep the laser disconnected.

1. Power relay from 5 V.
2. Use common GND between ESP32 and relay supply.
3. Verify transistor OFF leaves relay released.
4. Verify one commanded PICK pulls IN close to GND.
5. Verify RELEASE returns IN to its idle high level.
6. Verify a 180 ms PICK produces exactly one mechanical relay click cycle.
7. Enforce at least 250 ms rest between pulses.
8. Repeat cold boot, RESET and download-mode tests and prove zero unintended relay closure.

## Production gate

Do not change these defaults until the above tests pass:

```text
STAGECORE_LASER_ACTUATION_ENABLED=0
STAGECORE_LASER_OUTPUT_GPIO=-1
STAGECORE_LASER_SHARED_POWER_QUALIFIED=0
```

After the relay-only gate passes, add a dedicated qualified hardware environment
rather than modifying the CI/no-actuation environment in place.
