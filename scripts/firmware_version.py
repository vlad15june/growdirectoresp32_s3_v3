# Pre-build script: default the FIRMWARE_VERSION define from the VERSION file.
#
# The CI and the justfile pass the version explicitly:
#   PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='$(cat VERSION)'" pio run ...
# (bitbucket-pipelines.yml does exactly this; VERSION already contains the
# surrounding double quotes, so the macro expands to a C string literal).
#
# A bare `pio run` passes nothing, and ModuleConfig.h then falls back to a
# stale hardcoded "2.1.27" - a binary that silently reports the wrong version.
# This script closes that hole: when no FIRMWARE_VERSION arrives from the
# build flags, it supplies the one from the VERSION file.
# An explicitly passed flag always wins; the script then does nothing.

Import("env")

import os


def _version_already_defined(env):
    for flag in env.get("BUILD_FLAGS", []):
        if "FIRMWARE_VERSION" in str(flag):
            return True
    for define in env.get("CPPDEFINES", []):
        name = define[0] if isinstance(define, (tuple, list)) else define
        if str(name) == "FIRMWARE_VERSION":
            return True
    return False


if not _version_already_defined(env):
    version_path = os.path.join(env.subst("$PROJECT_DIR"), "VERSION")
    with open(version_path) as f:
        version = f.read().strip()
    # VERSION content includes the double quotes; escape them so they survive
    # the shell and the macro is the same string literal the CI
    # produces (-DFIRMWARE_VERSION=\"x.y.z\").
    env.Append(CPPDEFINES=[("FIRMWARE_VERSION", version.replace('"', '\\"'))])
