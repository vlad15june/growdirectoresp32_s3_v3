# Clean-room build environment for the ESP32-S3 firmware.
#
# This container is the REFERENCE build environment: reproducibility is defined
# as "two runs in fresh containers from this image produce byte-identical
# firmware.bin". A host build (macOS or other) is a working tool, not the
# reference; its binary legitimately differs from the container's, because the
# PlatformIO toolchain package 8.4.0+2021r2-patch5 is a different Espressif
# build per host OS (different newlib/libstdc++ bytes under the same version
# string), and because the absolute project path leaks into firmware.bin via
# the app descriptor's app_elf_sha256 (DWARF comp_dir in firmware.elf). Inside
# the container both are fixed: one toolchain build, one /tmp/src path.
#
# Everything the build needs is baked into the image at `docker build` time:
# the pinned PlatformIO core (requirements.txt), the pinned platform and
# toolchain packages (into PLATFORMIO_CORE_DIR), and the pinned libraries
# (into PLATFORMIO_LIBDEPS_DIR, outside the per-project workspace, so they
# survive the warmup cleanup and `docker run` does not touch the network --
# it can be run with --network=none).
#
# Build the image (once per change to requirements.txt/platformio.ini):
#
#   docker build -t gd-firmware-build .
#
# Run the build (from the repository root; GIT_COMMIT is required because the
# mounted checkout may be a git worktree whose gitdir is outside the mount):
#
#   docker run --rm --network=none -v "$PWD":/project \
#     -e GIT_COMMIT="$(git rev-parse HEAD)" gd-firmware-build
#
# The container copies the sources to the fixed path /tmp/src (the host .pio
# must not leak in, and the path must not depend on where the host checkout
# lives), builds esp32-s3-devkitm-1, and drops firmware.bin, firmware.elf and
# build_manifest.json into ./artifacts/ of the mounted repository.
#
# FIRMWARE_VERSION defaults to the VERSION file via scripts/firmware_version.py;
# override it the way the CI does:
#   -e PLATFORMIO_BUILD_FLAGS="-DFIRMWARE_VERSION='\"X.Y.Z\"'"
#
# The environment to build is selected with PIO_TARGET_ENV (default
# esp32-s3-devkitm-1). The source-built environment runs offline the same way:
#
#   docker run --rm --network=none -v "$PWD":/project \
#     -e GIT_COMMIT="$(git rev-parse HEAD)" \
#     -e PIO_TARGET_ENV=esp32-s3-devkitm-1-src gd-firmware-build
#
# Its extra needs are baked in below: the ESP-IDF and cmake/ninja packages, and
# the per-IDF-version python venv the espidf builder creates on first use - a
# scratch compile at image-build time triggers all of those downloads once.
#
# PIO_BUILD_JOBS caps compile parallelism for memory-tight VMs. Observed on
# colima: a 2 GiB VM does NOT build the source environment at any job count -
# a single cc1plus on the application's largest translation unit was OOM-killed
# even with -e PIO_BUILD_JOBS=1; a 4-CPU/6 GiB VM builds it with default jobs
# in ~90 s. Size the VM to at least 6 GiB and use PIO_BUILD_JOBS only to trade
# time for footprint on machines between those two points.

FROM python:3.11-slim-bookworm

ENV PLATFORMIO_CORE_DIR=/opt/pio-core \
    PLATFORMIO_LIBDEPS_DIR=/opt/pio-libdeps

# Two determinism pins for the source-built environment; the prebuilt
# environment's binary contains neither dates nor python-set orderings, so
# they change nothing for it.
# - esp_app_desc.c embeds __DATE__/__TIME__ (CONFIG_APP_COMPILE_TIME_DATE=y at
#   the factory, kept for parity); GCC substitutes SOURCE_DATE_EPOCH for both.
#   The value is 2024-12-12, the build date of the pinned framework package.
# - The CMake/ldgen pipeline iterates python string sets whose order
#   PYTHONHASHSEED randomizes per process.
ENV SOURCE_DATE_EPOCH=1733961600 \
    PYTHONHASHSEED=0

COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

# Pre-fetch the pinned platform, toolchain packages and libraries into the
# image, using the project's own platformio.ini as the single source of truth.
# The platform and tools land in PLATFORMIO_CORE_DIR, the libraries in
# PLATFORMIO_LIBDEPS_DIR; both live outside /tmp/warmup and persist in the
# image layer after the scratch project is removed.
COPY platformio.ini /tmp/warmup/platformio.ini
RUN cd /tmp/warmup \
    && mkdir -p src \
    && pio pkg install -e esp32-s3-devkitm-1 \
    && pio pkg install -e esp32-s3-devkitm-1-src \
    && rm -rf /tmp/warmup

# Warm what the source-built environment resolves on first build rather than
# at install time: the per-IDF-version python venv (pip packages) that the
# espidf builder creates under PLATFORMIO_CORE_DIR, and the CMake/kconfig
# machinery. A scratch project carrying the real build inputs - the IDF
# project files, the pinned sdkconfig and partition table, the extra_scripts -
# and a one-line application is compiled once at image-build time; its
# artifacts are discarded, the venv and packages persist in the layer, and
# `docker run --network=none` then configures and builds without the network.
COPY platformio.ini CMakeLists.txt sdkconfig.defaults default_8MB.csv VERSION /tmp/warmup-src/
COPY scripts/ /tmp/warmup-src/scripts/
COPY src/CMakeLists.txt /tmp/warmup-src/src/CMakeLists.txt
RUN cd /tmp/warmup-src \
    && printf '#include <Arduino.h>\nvoid setup() {}\nvoid loop() {}\n' > src/main.cpp \
    && pio run -e esp32-s3-devkitm-1-src \
    && rm -rf /tmp/warmup-src

WORKDIR /project

CMD ["/bin/sh", "-ec", "\
    : \"${GIT_COMMIT:?is required -- docker run -e GIT_COMMIT=<sha of the built commit>}\"; \
    PIO_TARGET_ENV=\"${PIO_TARGET_ENV:-esp32-s3-devkitm-1}\"; \
    mkdir -p /tmp/src; \
    tar -C /project --exclude=.pio --exclude=.git --exclude=artifacts -cf - . \
      | tar -C /tmp/src -xf -; \
    cd /tmp/src; \
    pio run ${PIO_BUILD_JOBS:+-j \"$PIO_BUILD_JOBS\"} -e \"$PIO_TARGET_ENV\"; \
    mkdir -p /project/artifacts; \
    cp \".pio/build/$PIO_TARGET_ENV/firmware.bin\" \
       \".pio/build/$PIO_TARGET_ENV/firmware.elf\" \
       \".pio/build/$PIO_TARGET_ENV/build_manifest.json\" /project/artifacts/; \
    if [ -f \".pio/build/$PIO_TARGET_ENV/bootloader.bin\" ]; then \
      cp \".pio/build/$PIO_TARGET_ENV/bootloader.bin\" /project/artifacts/; \
    fi; \
    echo 'Artifacts copied to ./artifacts:'; \
    cd /project/artifacts; \
    sha256sum firmware.bin; \
    if [ -f bootloader.bin ]; then sha256sum bootloader.bin; fi"]
