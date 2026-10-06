#!/usr/bin/env python3
"""Validate StageLaser first-boot serial logs without exposing credentials."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


FIRMWARE_RE = re.compile(r"StageLaser firmware\s+(\S+)\s+\(([^)]+)\)")


def evaluate(
    log: str,
    *,
    expected_revision: str | None = None,
    expect_provisioning: bool = False,
    expect_resync_required: bool = False,
) -> list[str]:
    failures: list[str] = []

    match = FIRMWARE_RE.search(log)
    if match is None:
        failures.append("missing StageLaser firmware identity line")
    elif expected_revision is not None and match.group(2) != expected_revision:
        failures.append(
            f"build revision mismatch: expected {expected_revision}, got {match.group(2)}"
        )

    required = (
        "NO-ACTUATION build: relay GPIO is intentionally disabled",
        "laser truth restored;",
        "device_id=",
    )
    for marker in required:
        if marker not in log:
            failures.append(f"missing required marker: {marker}")

    forbidden = (
        "actuation build requested but relay polarity is not qualified",
        "safe failure:",
    )
    for marker in forbidden:
        if marker in log:
            failures.append(f"forbidden/failure marker present: {marker}")

    if expect_resync_required:
        if "shared_power_qualified=no" not in log:
            failures.append(
                "missing cold-boot safety marker: shared_power_qualified=no"
            )
        if "resync_required=yes" not in log:
            failures.append(
                "cold boot did not require attended state resync"
            )

    if expect_provisioning:
        provisioning_required = (
            "first-run provisioning AP SSID=",
            "relay remains NO-ACTUATION; provisioning cannot assign a Project",
        )
        for marker in provisioning_required:
            if marker not in log:
                failures.append(f"missing provisioning marker: {marker}")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "logfile",
        nargs="?",
        help="serial log path; reads stdin when omitted",
    )
    parser.add_argument("--expected-revision")
    parser.add_argument("--expect-provisioning", action="store_true")
    parser.add_argument("--expect-resync-required", action="store_true")
    args = parser.parse_args()

    if args.logfile:
        log = Path(args.logfile).read_text(errors="replace")
    else:
        log = sys.stdin.read()

    failures = evaluate(
        log,
        expected_revision=args.expected_revision,
        expect_provisioning=args.expect_provisioning,
        expect_resync_required=args.expect_resync_required,
    )

    if failures:
        print("StageLaser first-boot qualification: FAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("StageLaser first-boot qualification: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
