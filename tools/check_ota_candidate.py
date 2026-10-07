#!/usr/bin/env python3
import argparse
from pathlib import Path

FLASH_SIZE_BYTES = 4 * 1024 * 1024
REQUIRED_PARTITIONS = {"nvs", "otadata", "phy_init", "ota_0", "ota_1", "storage"}


def _parse_int(value: str) -> int:
    return int(value.strip(), 0)


def load_partitions(path: Path):
    partitions = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue

        columns = [column.strip() for column in line.split(",")]
        if len(columns) < 5:
            raise ValueError(f"{path}:{lineno}: expected at least 5 CSV fields")

        name, part_type, subtype, offset_text, size_text = columns[:5]
        if not name:
            raise ValueError(f"{path}:{lineno}: partition name is empty")

        partitions.append(
            {
                "name": name,
                "type": part_type,
                "subtype": subtype,
                "offset": _parse_int(offset_text),
                "size": _parse_int(size_text),
            }
        )
    return partitions


def validate_layout(partitions, flash_size=FLASH_SIZE_BYTES):
    if not partitions:
        raise ValueError("partition table is empty")

    names = [partition["name"] for partition in partitions]
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValueError(f"duplicate partition names: {', '.join(duplicates)}")

    missing = sorted(REQUIRED_PARTITIONS.difference(names))
    if missing:
        raise ValueError(f"missing required partitions: {', '.join(missing)}")

    ordered = sorted(partitions, key=lambda partition: partition["offset"])
    previous_end = 0
    previous_name = None

    for partition in ordered:
        offset = partition["offset"]
        size = partition["size"]
        if offset < 0 or size <= 0:
            raise ValueError(f"{partition['name']}: invalid offset/size")

        end = offset + size
        if end > flash_size:
            raise ValueError(
                f"{partition['name']}: end 0x{end:x} exceeds 4 MB flash 0x{flash_size:x}"
            )
        if previous_name is not None and offset < previous_end:
            raise ValueError(
                f"{partition['name']}: overlaps previous partition {previous_name}"
            )

        previous_end = end
        previous_name = partition["name"]

    by_name = {partition["name"]: partition for partition in partitions}
    ota_0 = by_name["ota_0"]
    ota_1 = by_name["ota_1"]
    otadata = by_name["otadata"]

    if ota_0["type"] != "app" or ota_0["subtype"] != "ota_0":
        raise ValueError("ota_0 must be app/ota_0")
    if ota_1["type"] != "app" or ota_1["subtype"] != "ota_1":
        raise ValueError("ota_1 must be app/ota_1")
    if ota_0["size"] != ota_1["size"]:
        raise ValueError("OTA app slots must have equal size")
    if ota_0["offset"] % 0x10000 or ota_1["offset"] % 0x10000:
        raise ValueError("OTA app slots must start on 64 KiB boundaries")

    if otadata["type"] != "data" or otadata["subtype"] != "ota":
        raise ValueError("otadata must be data/ota")
    if otadata["size"] < 0x2000:
        raise ValueError("otadata must reserve at least 0x2000 bytes")

    return ota_0["size"]


def validate_firmware(path: Path, slot_size: int):
    firmware_size = path.stat().st_size
    if firmware_size <= 0:
        raise ValueError("firmware artifact is empty")
    if firmware_size > slot_size:
        raise ValueError(
            f"firmware.bin is {firmware_size} bytes but OTA slot is {slot_size} bytes"
        )
    return firmware_size


def main():
    parser = argparse.ArgumentParser(
        description="Validate the StageLaser 4 MB dual-slot OTA candidate layout."
    )
    parser.add_argument("--partitions", type=Path, required=True)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args()

    slot_size = validate_layout(load_partitions(args.partitions))
    print(f"OTA layout valid; app slot size={slot_size} bytes")

    if args.firmware is not None:
        firmware_size = validate_firmware(args.firmware, slot_size)
        print(
            f"Firmware fits OTA slot; firmware={firmware_size} bytes, "
            f"headroom={slot_size - firmware_size} bytes"
        )


if __name__ == "__main__":
    main()
