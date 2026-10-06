# StageLaser OTA / recovery candidate

This is a **software candidate only** for the verified 4 MB ESP32-C3 unit.

It does not make OTA production-ready and does not authorize relay actuation.

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

The current firmware image fits inside a 0x1A0000 app slot with headroom.

## Why this is still only a candidate

Production qualification still requires:

1. prove first flash / boot using this partition map on the actual board
2. implement the controlled StageCore OTA transport and authorization path
3. verify image integrity and version policy
4. verify the bootloader rollback configuration
5. confirm the new image only becomes accepted after StageLaser reaches a safe healthy state
6. power-loss tests during download and boot selection
7. failed-image rollback test
8. Hub loss / Wi-Fi loss during update
9. preserve device identity, trust, config, truth-state and replay fence across OTA
10. define a physical attended recovery / trust-reset flow

## Safety rules for a future updater

Before update starts:

- force DISARMED
- stop local Flash
- settle deterministic OFF only when the physical state is known
- refuse OTA while physical state is UNKNOWN unless the update is explicitly attended
- revoke ACTIVE runtime command authority during the update
- never create a relay pulse as part of update or reboot handling

After booting a new image:

- start DISARMED
- do not restore Project / Runtime Snapshot authority from persistent storage
- preserve physical truth conservatively
- require normal Stage Device v2 authentication / assignment again
- do not mark the new image healthy until core persistence and runtime prerequisites pass

## CI role

The PlatformIO environment:

```text
esp32c3-ota-candidate-no-actuation
```

exists only to prove the current firmware builds against this candidate geometry.

It remains:

```text
STAGECORE_LASER_ACTUATION_ENABLED=0
STAGECORE_LASER_OUTPUT_GPIO=-1
STAGECORE_LASER_SHARED_POWER_QUALIFIED=0
```

The normal default environment remains `esp32c3-ci-no-actuation`.
