# Post-build script: writes build_manifest.json next to firmware.bin.
#
# The manifest is the build's passport: every resolved version that shaped the
# binary (PlatformIO core, platform, toolchain packages, libraries), the build
# flags and the effective FIRMWARE_VERSION, the git commit it was built from,
# and the sha256 of the binary itself. Two builds of the same commit are
# expected to produce identical manifests; if they do not, the differing field
# names the source of nondeterminism.
#
# The manifest intentionally contains no timestamps and no absolute paths, so
# that it can be diffed between machines as-is.
#
# Two hooks keep it in step with the build:
# - a post-action on firmware.bin regenerates it whenever the binary is
#   (re)built;
# - a post-action on checkprogsize (which PlatformIO runs on every `pio run`,
#   even a no-op one) rewrites it unconditionally, so that a passport can never
#   describe inputs the build no longer has.
#
# The second hook rewrites rather than compares mtimes because most of what the
# manifest records is not the binary: the sdkconfig defaults and the resolved
# per-environment config, the partition table, the git commit and dirty flag,
# the resolved package versions. Any of those can change while firmware.bin
# stays untouched - editing an sdkconfig overlay that the committed resolved
# config overrides is exactly that case, and it exits 0 - and a manifest
# regenerated only when the binary is newer would keep showing the previous
# inputs. Hashing costs milliseconds; a passport that can lie costs whoever
# reads two of its fields and believes them.

Import("env")

import hashlib
import json
import os
import subprocess


def _git(args, cwd):
    try:
        out = subprocess.check_output(
            ["git"] + args, cwd=cwd, stderr=subprocess.DEVNULL
        )
        return out.decode("utf-8", "replace").strip()
    except Exception:
        return None


def _firmware_version(env):
    # The effective -DFIRMWARE_VERSION, whether it came from external build
    # flags (CI / justfile) or from scripts/firmware_version.py.
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (tuple, list)) and str(define[0]) == "FIRMWARE_VERSION":
            return str(define[1]).strip('\\"')
    for flag in env.get("BUILD_FLAGS", []):
        flag = str(flag)
        if "FIRMWARE_VERSION" in flag and "=" in flag:
            return flag.split("=", 1)[1].strip().strip("'").strip('"')
    return None  # ModuleConfig.h falls back to its hardcoded default


def _sha256_file(path):
    try:
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()
    except (OSError, TypeError):
        return None


def _bootloader(env, build_dir):
    # The bootloader is part of what a cable flash puts on the device, so the
    # manifest names whose bootloader this environment carries. Both build
    # paths produce one: an espidf-based environment compiles its own from the
    # ESP-IDF sources, the prebuilt environments convert the vendor ELF shipped
    # inside the framework package (tools/sdk/<mcu>/bin) - a clean build leaves
    # bootloader.bin in the build directory either way. Origin and hash are
    # recorded together or not at all: should some future environment produce
    # no bootloader image, the manifest omits the block rather than pairing a
    # claimed origin with an absent hash.
    sha256 = _sha256_file(os.path.join(build_dir, "bootloader.bin"))
    if sha256 is None:
        return None
    built_from_source = "espidf" in (env.get("PIOFRAMEWORK") or [])
    return {
        "origin": "built-from-source" if built_from_source else "vendor-prebuilt",
        "sha256": sha256,
    }


def _sdkconfig_defaults_files(env, project_dir):
    # Which defaults files ESP-IDF was told to read, in order. Environments that
    # pass none get the single sdkconfig.defaults ESP-IDF picks up by itself; the
    # TLS buffer-profile environments pass a list through SDKCONFIG_DEFAULTS in
    # board_build.cmake_extra_args, and recording only the first of them would
    # describe two different builds identically.
    extra_args = env.BoardConfig().get("build.cmake_extra_args", "") or ""
    names = ["sdkconfig.defaults"]
    for arg in extra_args.split():
        if arg.startswith("-DSDKCONFIG_DEFAULTS="):
            names = [n for n in arg.split("=", 1)[1].split(";") if n]
            break
    return [
        {"path": name, "sha256": _sha256_file(os.path.join(project_dir, name))}
        for name in names
    ]


def _framework_sources(env, project_dir):
    # For environments that build the framework from source (framework =
    # arduino, espidf) the package versions alone do not pin the build: the
    # ESP-IDF configuration and the partition table shape the binary just as
    # much. Recorded as hashes of the exact files the build consumed - every
    # defaults file it was given, the resolved per-environment sdkconfig, and the
    # partition CSV - so two manifests agree exactly when the sources agree.
    if "espidf" not in (env.get("PIOFRAMEWORK") or []):
        return None
    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV") or None
    return {
        "sdkconfig_defaults": _sdkconfig_defaults_files(env, project_dir),
        "sdkconfig_resolved_sha256": _sha256_file(
            os.path.join(project_dir, "sdkconfig.%s" % env["PIOENV"])
        ),
        "partition_table_csv_sha256": _sha256_file(partitions_csv),
    }


def _write_manifest(env):
    from platformio import __version__ as pio_core_version

    try:
        import SCons

        scons_version = str(SCons.__version__)
    except Exception:
        scons_version = None

    platform = env.PioPlatform()
    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    bin_path = os.path.join(build_dir, "firmware.bin")

    with open(bin_path, "rb") as f:
        bin_sha256 = hashlib.sha256(f.read()).hexdigest()

    # In a container the checkout may be mounted without a usable .git
    # (e.g. a worktree whose gitdir lives outside the mount); the caller
    # then supplies the commit via the GIT_COMMIT environment variable.
    commit = _git(["rev-parse", "HEAD"], project_dir)
    if commit is None:
        commit = os.environ.get("GIT_COMMIT", "unknown")
    porcelain = _git(["status", "--porcelain"], project_dir)

    manifest = {
        "environment": env["PIOENV"],
        "board": env.get("BOARD"),
        "firmware_version": _firmware_version(env),
        "build_flags": [str(f) for f in env.get("BUILD_FLAGS", [])],
        "platformio_core": str(pio_core_version),
        "scons": scons_version,
        "platform": {
            "name": str(platform.name),
            "version": str(platform.version),
        },
        "packages": {
            p["name"]: p["version"] for p in platform.dump_used_packages()
        },
        "libraries": {
            str(lb.name): (str(lb.version) if lb.version else None)
            for lb in env.GetLibBuilders()
        },
        "git_commit": commit,
        "git_dirty": (bool(porcelain) if porcelain is not None else None),
        "firmware_sha256": bin_sha256,
    }

    bootloader = _bootloader(env, build_dir)
    if bootloader is not None:
        manifest["bootloader"] = bootloader

    framework_sources = _framework_sources(env, project_dir)
    if framework_sources is not None:
        manifest["framework_sources"] = framework_sources

    out_path = os.path.join(build_dir, "build_manifest.json")
    with open(out_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    print("build_manifest.json written; firmware sha256=%s" % bin_sha256)


def _on_bin_built(target, source, env):
    _write_manifest(env)


def _rewrite_beside_current_binary(target, source, env):
    build_dir = env.subst("$BUILD_DIR")
    bin_path = os.path.join(build_dir, "firmware.bin")
    if not os.path.isfile(bin_path):
        return  # firmware.bin is built after this action on a fresh build;
        # the post-action on the binary itself covers that case.
    _write_manifest(env)


env.AddPostAction("$BUILD_DIR/firmware.bin", _on_bin_built)
env.AddPostAction("checkprogsize", _rewrite_beside_current_binary)
