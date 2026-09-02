# SPDX-License-Identifier: GPL-3.0-or-later
#
# DC-UPS-2CH — PlatformIO pre-script that injects build-time firmware identity
# into the compilation as -D macros:
#
#   FW_VERSION     — semantic version, read from the top-level VERSION file
#                    (env var VERSION wins if it is set — used by the root
#                    Makefile so `make firmware` and `pio run` stay in sync)
#   FW_GIT_HASH    — short commit hash from `git rev-parse --short HEAD`,
#                    with "-dirty" appended if the working tree has changes
#                    (env var GIT_HASH wins)
#   FW_BUILD_DATE  — UTC build date YYYY-MM-DD (env var BUILD_DATE wins)
#
# The header include/version.h supplies fallbacks so the firmware still
# builds if this script never runs (e.g. Arduino IDE).

import datetime
import os
import subprocess
from pathlib import Path

Import("env")  # noqa: F821  (SConscript-injected)

REPO_ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821


def _git(*args) -> str:
    try:
        out = subprocess.check_output(
            ["git", "-C", str(REPO_ROOT), *args],
            stderr=subprocess.DEVNULL,
        )
        return out.decode("utf-8", errors="replace").strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def resolve_version() -> str:
    env_val = os.environ.get("VERSION", "").strip()
    if env_val:
        return env_val

    version_file = REPO_ROOT / "VERSION"
    if version_file.is_file():
        text = version_file.read_text(encoding="utf-8").strip()
        if text:
            return text.splitlines()[0].strip()

    return "dev"


def resolve_git_hash() -> str:
    env_val = os.environ.get("GIT_HASH", "").strip()
    if env_val:
        return env_val

    short = _git("rev-parse", "--short", "HEAD")
    if not short:
        return "unknown"

    dirty = _git("status", "--porcelain")
    if dirty:
        short += "-dirty"
    return short


def resolve_build_date() -> str:
    env_val = os.environ.get("BUILD_DATE", "").strip()
    if env_val:
        return env_val
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")


version    = resolve_version()
git_hash   = resolve_git_hash()
build_date = resolve_build_date()

env.Append(CPPDEFINES=[  # noqa: F821
    ("FW_VERSION",    f'\\"{version}\\"'),
    ("FW_GIT_HASH",   f'\\"{git_hash}\\"'),
    ("FW_BUILD_DATE", f'\\"{build_date}\\"'),
])

print(f"[inject_version] FW_VERSION={version} "
      f"FW_GIT_HASH={git_hash} FW_BUILD_DATE={build_date}")
