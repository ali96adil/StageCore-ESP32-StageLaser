# StageLaser first-boot qualification

Use this only with the **relay and laser completely disconnected**.

The first hardware flash is intended to validate the ESP32-C3 board, persistent
identity, NO-ACTUATION boot path, provisioning and StageCore connectivity before
any relay GPIO is selected.

## Preconditions

- repository is on the approved `main` revision
- PlatformIO environment: `esp32c3-ci-no-actuation`
- `STAGECORE_LASER_ACTUATION_ENABLED=0`
- `STAGECORE_LASER_OUTPUT_GPIO=-1`
- `STAGECORE_LASER_SHARED_POWER_QUALIFIED=0`
- relay disconnected
- laser disconnected

## First flash

Build before upload:

```bash
EXPECTED_SHA="$(git rev-parse HEAD)" \
pio run -e esp32c3-ci-no-actuation
```

Then upload to the explicitly identified native USB port.

The application console is intentionally configured for the ESP32-C3 native
USB Serial/JTAG controller. After flashing, monitor the same `/dev/cu.usbmodem*`
device at 115200 baud; do not expect StageLaser logs on the UART0 GPIO pins.

Do not use the OTA-candidate environment for this first hardware qualification.

## Serial observations

The first boot must contain:

```text
StageLaser firmware ...
NO-ACTUATION build: relay GPIO is intentionally disabled
laser truth restored; ...
device_id=...
```

It must **not** contain:

```text
actuation build requested but relay polarity is not qualified
safe failure:
```

On an unconfigured device, first-run provisioning should also report a
`StageLaser-...` AP and explicitly state that the relay remains NO-ACTUATION.

The generated AP password is a local credential. Do not paste it into public
issues or repository documentation.

## Automated check

Save/capture the serial text and run:

```bash
python tools/check_first_boot_log.py first-boot.log \
  --expected-revision "$(git rev-parse HEAD)" \
  --expect-provisioning
```

A clean bootstrap returns:

```text
StageLaser first-boot qualification: PASS
```

The checker intentionally does not print the provisioning password.

## What this does not qualify

A successful first boot does not qualify:

- GPIO3
- transistor/MOSFET driver
- relay pulse polarity
- relay pulse width/rest timing
- laser dry-contact wiring
- shared-power behavior
- OTA/recovery production behavior

Those remain separate attended hardware gates.
