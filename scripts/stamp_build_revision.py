from pathlib import Path
import os
import subprocess

Import("env")  # type: ignore[name-defined]

def git_sha():
    expected = os.environ.get("EXPECTED_SHA", "").strip()
    if expected:
        return expected
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    except Exception:
        return "unknown"

sha = git_sha()
env.Append(CPPDEFINES=[("STAGECORE_BUILD_REVISION", '\"%s\"' % sha)])
Path(".stagecore-build-revision").write_text(sha + "\n")
