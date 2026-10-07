# StageLaser OTA / recovery qualification

## Current status

The controlled StageCore OTA **software path is implemented end-to-end** for the
verified 4 MB ESP32-C3 StageLaser board.

This does **not** make OTA physically qualified and does **not** authorize relay
or laser actuation.

The default image remains NO-ACTUATION and does not advertise firmware
maintenance. OTA is exposed only by the dedicated 4 MB candidate environment:

```text
esp32c3-ota-candidate-no-actuation
```

Its safety gates remain:

```text
STAGECORE_LASER_ACTUATION_ENABLED=0
STAGECORE_LASER_OUTPUT_GPIO=-1
STAGECORE_LASER_SHARED_POWER_QUALIFIED=0
STAGECORE_LASER_LOCAL_RECOVERY_GPIO=-1
STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED=0
```

## Implemented software path

The OTA candidate now implements all of the following:

- candidate-only `device.maintenance.firmware-update` capability
- exact authenticated Stage Device v2 session/device/profile binding
- short-lived QUALIFIED manifest validation
- exact current/target firmware version fencing
- exact source revision, artifact size and SHA-256 requirements
- Hub-local HTTPS download only; arbitrary external URLs are rejected
- existing pinned Hub certificate + active `StageCoreSession`
- trusted UTC manifest lifetime validation
- streaming write only to the inactive OTA slot
- exact byte-count and SHA-256 verification
- ESP image verification before boot selection
- persistent expected `update_id`, target version and source revision before reboot
- canonical progress:
  `ACCEPTED -> DOWNLOADING -> WRITING -> VERIFYING -> REBOOTING`
- ESP-IDF application rollback support in the OTA candidate
- pending-image confirmation only after the local StageLaser safe-boot checkpoint
- exact post-boot compiled version + source-revision match before confirmation
- automatic rollback path when the pending candidate cannot be safely confirmed
- no relay pulse as part of download, write, reboot, rollback or recovery handling

StageCore Core supplies the complementary maintenance authority path:

1. register exact firmware bytes as `QUALIFIED`
2. issue a short-lived manifest to one exact device
3. explicitly send the maintenance request
4. record ordered progress outside Cue/show authority
5. close the lifecycle after authenticated post-reboot reconnect as
   `COMPLETED` or `ROLLBACK_OBSERVED`

## Candidate partition map

```text
nvs       0x009000  0x06000
otadata   0x00F000  0x02000
phy_init  0x011000  0x01000
ota_0     0x020000  0x1A0000
ota_1     0x1C0000  0x1A0000
storage   0x360000  0x0A0000
end       0x400000
```

Properties:

- total flash span ends exactly at 4 MB
- dual equal OTA app slots
- each app slot is 0x1A0000 bytes (1,703,936 bytes)
- OTA data partition is 0x2000 bytes
- app offsets are 0x10000-aligned
- no factory app is included; ota_0 is the first app slot
- 0x0A0000 bytes remain for storage
- the existing NVS allocation remains 0x6000 bytes
- CI proves the built OTA-candidate image fits an app slot

The real board flash capacity has already been verified as 4 MB. The remaining
gate is physical behavior, not flash-capacity discovery.

## Physical qualification boundary

Perform OTA qualification with the **relay and laser disconnected**.

Required prerequisites:

- normal first-boot NO-ACTUATION qualification has passed
- exact approved firmware revisions are known
- candidate artifacts come from clean, revision-stamped builds
- the uploaded bytes, SHA-256 and source revision are recorded
- StageCore shows the target StageLaser authenticated, ONLINE and READY
- StageLaser reports DISARMED + stable known OFF
- no Flash is active
- no pulse is in progress
- no operational SHOW/REHEARSAL session owns the device

Do not enable an output GPIO merely to test OTA.

## Physical OTA acceptance sequence

### 1. Candidate first boot

Flash the OTA candidate over native USB with relay and laser disconnected.

Acceptance:

- boots on the verified 4 MB partition layout
- remains DISARMED
- performs no relay/output actuation
- preserves persistent identity/configuration
- reconnects through the pinned Hub path
- advertises firmware maintenance only in the candidate image

### 2. Successful controlled update

Use two distinct approved candidate builds so the target version/source revision
can be observed unambiguously.

From the Stage Devices UI:

1. register the exact target `firmware.bin` as `QUALIFIED`
2. enter the exact target version
3. enter the exact 40-character lowercase source revision
4. enter the exact lowercase SHA-256
5. issue the maintenance manifest
6. send the maintenance request

Observe the lifecycle:

```text
SENT
ACCEPTED
DOWNLOADING
WRITING
VERIFYING
REBOOTING
COMPLETED
```

Acceptance requires:

- the device downloads only from the pinned StageCore Hub
- bytes written equal the registered artifact size
- SHA-256 matches before boot selection
- only the inactive app slot is written
- reboot produces no physical actuation
- the device reconnects on a newer authenticated connection generation
- reported firmware version equals the target version
- compiled source revision equals the qualified source revision
- StageCore records `COMPLETED`
- persistent device identity/trust/configuration survive the update

### 3. Interrupted pre-reboot update

Repeat with relay and laser disconnected and interrupt Wi-Fi/Hub availability
before `REBOOTING`.

Acceptance:

- no relay/output actuation
- no unverified image is selected for boot
- lifecycle ends fail-closed as interrupted/failed according to the stage reached
- the previously bootable image remains available

### 4. Failed-candidate rollback

Use only a dedicated NO-ACTUATION rollback-test candidate whose failure mode is
known in advance and cannot reach relay actuation.

Acceptance:

- pending candidate is never marked valid before the local safe-boot checkpoint
- failed/non-confirmed candidate triggers the ESP-IDF rollback path
- previous known-good image boots
- StageCore observes the same authenticated device/profile after reconnect
- lifecycle closes as `ROLLBACK_OBSERVED`
- no relay/output actuation occurs during either boot

Do not manufacture this test by falsifying QUALIFIED metadata for an ordinary
production candidate.

### 5. Hub restart around reboot

Exercise a Hub restart while the device is in the expected reboot window.

Acceptance:

- persisted `REBOOTING` lifecycle survives the Hub restart
- post-reboot reconnect closes against the same update ID and exact device
- target version produces `COMPLETED`
- previous version produces `ROLLBACK_OBSERVED`
- no unrelated device can close the update

### 6. Power-loss boundary

With relay and laser disconnected, exercise loss of ESP power during:

- download
- inactive-slot write
- after verification but before normal reconnect

Acceptance is always fail-closed:

- boot remains possible from a valid image
- no physical output is produced
- a partially written slot is never treated as confirmed
- identity/trust/configuration remain intact or fail safely

## Recovery qualification

The boot-time physical-presence Hub trust recovery source path is already
implemented and deliberately non-actuating.

It remains disabled:

```text
STAGECORE_LASER_LOCAL_RECOVERY_GPIO=-1
STAGECORE_LASER_LOCAL_RECOVERY_QUALIFIED=0
```

Before enabling it, physically qualify:

- chosen GPIO
- active level
- idle bias
- boot/reset behavior
- accidental-trigger resistance
- operation while already DISARMED + stable known OFF

Recovery must clear only remembered Hub trust. It must preserve Wi-Fi,
persistent device identity, StageLaser physical truth, timing limits and replay
fencing.

## Release gate

OTA software completion is **not** the release gate.

StageLaser OTA becomes physically qualified only after the real-board tests
above pass and their observations are recorded in the hardware tracker.

Until then:

- keep actuation disabled
- keep output GPIO unset
- keep shared-power unqualified
- keep local recovery disabled
- do not call the StageLaser V1 hardware/show qualification complete
